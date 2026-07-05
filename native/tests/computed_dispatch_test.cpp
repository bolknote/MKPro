#include "mkpro/compiler.hpp"
#include "mkpro/core/address_formula_solver.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/dead_code_after_halt.hpp"
#include "mkpro/core/result.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

using core::passes::dead_code_after_halt;
using core::passes::PassContext;
using core::passes::PassResult;

IrOp make_plain(std::string name) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.name = std::move(name);
  op.opcode = 0x00;
  return op;
}

IrOp make_label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp make_computed_dispatch(const std::string& comment) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.opcode = 0x8e;  // К БП e
  op.meta.comment = comment;
  return op;
}

bool has_plain(const std::vector<IrOp>& ops, const std::string& name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Plain && op.name == name;
  });
}

// entry -> computed К БП (no static fallthrough) -> three case bodies whose only
// reachability is the computed jump's advertised target labels.
std::vector<IrOp> dispatch_ir(const std::string& jump_comment) {
  std::vector<IrOp> ops;
  ops.push_back(make_label("entry"));
  ops.push_back(make_plain("load_selector"));
  ops.push_back(make_computed_dispatch(jump_comment));
  ops.push_back(make_label("case_a"));
  ops.push_back(make_plain("body_a"));
  ops.push_back(make_label("case_b"));
  ops.push_back(make_plain("body_b"));
  ops.push_back(make_label("case_c"));
  ops.push_back(make_plain("body_c"));
  return ops;
}

}  // namespace

void computed_dispatch_targets_survive_dead_code_elimination() {
  const CompileOptions options;
  const PassContext context{.options = options};

  // Control: without the target marker the computed jump exposes no successors,
  // so the case bodies are genuinely unreachable and the DCE pass removes them.
  // This proves the survival assertion below is meaningful.
  {
    const PassResult removed = dead_code_after_halt(dispatch_ir("computed dispatch"), context);
    require(!has_plain(removed.ops, "body_a"),
            "unmarked computed-jump case body A must be dead-code eliminated");
    require(!has_plain(removed.ops, "body_b"),
            "unmarked computed-jump case body B must be dead-code eliminated");
    require(!has_plain(removed.ops, "body_c"),
            "unmarked computed-jump case body C must be dead-code eliminated");
  }

  // With the target marker the reachability scan follows the advertised edges, so
  // every dispatched case body is retained even though no direct jump references
  // it. This is the unblock that makes a computed dispatch legal in the pipeline.
  {
    const PassResult kept = dead_code_after_halt(
        dispatch_ir("computed dispatch; computed-dispatch-targets=case_a,case_b,case_c"), context);
    require(has_plain(kept.ops, "body_a"),
            "marked computed-dispatch must keep case body A reachable");
    require(has_plain(kept.ops, "body_b"),
            "marked computed-dispatch must keep case body B reachable");
    require(has_plain(kept.ops, "body_c"),
            "marked computed-dispatch must keep case body C reachable");
  }

  {
    const PassResult removed =
        dead_code_after_halt(dispatch_ir(
                                 "note: computed dispatch; computed-dispatch-targets=case_a,"
                                 "case_b,case_c"),
                             context);
    require(!has_plain(removed.ops, "body_a"),
            "embedded computed-dispatch marker must not keep case body A reachable");
    require(!has_plain(removed.ops, "body_b"),
            "embedded computed-dispatch marker must not keep case body B reachable");
    require(!has_plain(removed.ops, "body_c"),
            "embedded computed-dispatch marker must not keep case body C reachable");
  }
}

namespace {

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const Diagnostic& item) { return item.severity == DiagnosticSeverity::Error; });
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(),
                     [&](const ProofReport& proof) { return proof.id == id; });
}

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

std::string capability_status(const CompileResult& result, const std::string& id) {
  const auto it =
      std::find_if(result.optimizer.capabilities.begin(), result.optimizer.capabilities.end(),
                   [&](const OptimizerCapabilityReport& capability) {
                     return capability.id == id;
                   });
  return it == result.optimizer.capabilities.end() ? "" : it->status;
}

OptimizationReport optimization_report(std::string name) {
  OptimizationReport report;
  report.name = std::move(name);
  return report;
}

ProofReport proved_report(std::string id) {
  ProofReport report;
  report.id = std::move(id);
  report.status = "proved";
  report.detail = "unit-test proof";
  return report;
}

PreloadReport flow_preload(std::string register_name, std::string value) {
  return PreloadReport{
      .register_name = std::move(register_name),
      .value = std::move(value),
      .counts_against_program = false,
  };
}

ResolvedStep resolved_step(int opcode, std::string comment) {
  ResolvedStep step;
  step.opcode = opcode;
  step.comment = std::move(comment);
  return step;
}

ResolvedStep resolved_step_with_mnemonic(int opcode, std::string mnemonic, std::string comment) {
  ResolvedStep step = resolved_step(opcode, std::move(comment));
  step.mnemonic = std::move(mnemonic);
  return step;
}

ResolvedStep resolved_step_at(int address, int opcode, std::string comment) {
  ResolvedStep step = resolved_step(opcode, std::move(comment));
  step.address = address;
  return step;
}

ResolvedStep resolved_step_with_mnemonic_at(int address, int opcode, std::string mnemonic,
                                            std::string comment) {
  ResolvedStep step = resolved_step_with_mnemonic(opcode, std::move(mnemonic),
                                                 std::move(comment));
  step.address = address;
  return step;
}

std::string indirect_target_comment(const std::string& register_name, const std::string& value,
                                    int target) {
  return "preloaded R" + register_name + "=" + value + " indirect-target=" +
         std::to_string(target) + " indirect flow";
}

std::string runtime_indirect_target_comment(const std::string& register_name,
                                            const std::string& value, int target) {
  return "runtime indirect call; " + indirect_target_comment(register_name, value, target);
}

std::string computed_dispatch_comment(const SynthesizedDispatchPlan& plan,
                                      const std::vector<std::string>& labels) {
  std::string out = "computed dispatch; computed-dispatch-targets=";
  for (std::size_t index = 0; index < labels.size(); ++index) {
    if (index > 0)
      out += ",";
    out += labels.at(index);
  }
  out += "; computed-dispatch-proof-targets=";
  for (std::size_t index = 0; index < plan.proof_constraints.size(); ++index) {
    if (index > 0)
      out += ",";
    out += std::to_string(plan.proof_constraints.at(index).target);
  }
  return out;
}

std::filesystem::path project_file(std::filesystem::path relative_path) {
  const std::vector<std::filesystem::path> starts = {
      std::filesystem::path(__FILE__).parent_path(),
      std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path(),
      (std::filesystem::current_path() / std::filesystem::path(__FILE__)).parent_path(),
      std::filesystem::current_path(),
  };
  for (std::filesystem::path current : starts) {
    while (true) {
      const std::filesystem::path candidate = current / relative_path;
      if (std::filesystem::exists(candidate))
        return candidate;
      if (!current.has_parent_path() || current == current.parent_path())
        break;
      current = current.parent_path();
    }
  }
  return relative_path;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  require(file.good(), "test fixture source file should be readable: " + path.string());
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

bool identifier_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string source_without_comments(const std::string& source) {
  enum class State {
    Code,
    LineComment,
    BlockComment,
    StringLiteral,
    CharLiteral,
  };

  State state = State::Code;
  bool escaped = false;
  std::string out;
  out.reserve(source.size());
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char ch = source.at(index);
    const char next = index + 1U < source.size() ? source.at(index + 1U) : '\0';
    switch (state) {
      case State::Code:
        if (ch == '/' && next == '/') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::LineComment;
        } else if (ch == '/' && next == '*') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::BlockComment;
        } else if (ch == '"') {
          out.push_back(ch);
          state = State::StringLiteral;
          escaped = false;
        } else if (ch == '\'') {
          out.push_back(ch);
          state = State::CharLiteral;
          escaped = false;
        } else {
          out.push_back(ch);
        }
        break;
      case State::LineComment:
        if (ch == '\n') {
          out.push_back('\n');
          state = State::Code;
        } else {
          out.push_back(' ');
        }
        break;
      case State::BlockComment:
        if (ch == '*' && next == '/') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::Code;
        } else {
          out.push_back(ch == '\n' ? '\n' : ' ');
        }
        break;
      case State::StringLiteral:
      case State::CharLiteral:
        out.push_back(ch);
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if ((state == State::StringLiteral && ch == '"') ||
                   (state == State::CharLiteral && ch == '\'')) {
          state = State::Code;
        }
        break;
    }
  }
  return out;
}

std::string code_without_comments_and_literals(const std::string& source) {
  enum class State {
    Code,
    LineComment,
    BlockComment,
    StringLiteral,
    CharLiteral,
  };

  State state = State::Code;
  bool escaped = false;
  std::string out;
  out.reserve(source.size());
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char ch = source.at(index);
    const char next = index + 1U < source.size() ? source.at(index + 1U) : '\0';
    switch (state) {
      case State::Code:
        if (ch == '/' && next == '/') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::LineComment;
        } else if (ch == '/' && next == '*') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::BlockComment;
        } else if (ch == '"') {
          out.push_back(' ');
          state = State::StringLiteral;
          escaped = false;
        } else if (ch == '\'') {
          out.push_back(' ');
          state = State::CharLiteral;
          escaped = false;
        } else {
          out.push_back(ch);
        }
        break;
      case State::LineComment:
        if (ch == '\n') {
          out.push_back('\n');
          state = State::Code;
        } else {
          out.push_back(' ');
        }
        break;
      case State::BlockComment:
        if (ch == '*' && next == '/') {
          out.push_back(' ');
          out.push_back(' ');
          ++index;
          state = State::Code;
        } else {
          out.push_back(ch == '\n' ? '\n' : ' ');
        }
        break;
      case State::StringLiteral:
      case State::CharLiteral:
        out.push_back(ch == '\n' ? '\n' : ' ');
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if ((state == State::StringLiteral && ch == '"') ||
                   (state == State::CharLiteral && ch == '\'')) {
          state = State::Code;
        }
        break;
    }
  }
  return out;
}

bool contains_function_call_token(const std::string& code, const std::string& name) {
  std::size_t position = code.find(name);
  while (position != std::string::npos) {
    const bool left_ok = position == 0 || !identifier_char(code.at(position - 1U));
    std::size_t cursor = position + name.size();
    while (cursor < code.size() && std::isspace(static_cast<unsigned char>(code.at(cursor))) != 0)
      ++cursor;
    if (left_ok && cursor < code.size() && code.at(cursor) == '(')
      return true;
    position = code.find(name, position + 1U);
  }
  return false;
}

bool contains_emulator_include(const std::string& code) {
  std::istringstream lines(code);
  std::string line;
  while (std::getline(lines, line)) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    const std::string_view rest(line.data() + first, line.size() - first);
    if (rest.size() >= std::string_view("#include").size() &&
        rest.compare(0, std::string_view("#include").size(), "#include") == 0 &&
        rest.find("mkpro/emulator") != std::string_view::npos)
      return true;
  }
  return false;
}

bool contains_noncomment_line_text(const std::string& text, const std::string& needle) {
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.at(first) == '#')
      continue;
    if (line.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

int count_noncomment_lines_with_text(const std::string& text, const std::string& needle) {
  int count = 0;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.at(first) == '#')
      continue;
    if (line.find(needle) != std::string::npos)
      ++count;
  }
  return count;
}

std::string required_source_range(const std::string& source, const std::string& begin_marker,
                                  const std::string& end_marker) {
  const std::size_t begin = source.find(begin_marker);
  require(begin != std::string::npos, "source boundary begin marker should exist: " + begin_marker);
  const std::size_t end = source.find(end_marker, begin);
  require(end != std::string::npos, "source boundary end marker should exist: " + end_marker);
  return source.substr(begin, end - begin);
}

std::string required_source_range_after(const std::string& source, const std::string& anchor_marker,
                                        const std::string& begin_marker,
                                        const std::string& end_marker) {
  const std::size_t anchor = source.find(anchor_marker);
  require(anchor != std::string::npos, "source boundary anchor marker should exist: " + anchor_marker);
  const std::size_t begin = source.find(begin_marker, anchor);
  require(begin != std::string::npos,
          "source boundary begin marker should exist after anchor: " + begin_marker);
  const std::size_t end = source.find(end_marker, begin);
  require(end != std::string::npos, "source boundary end marker should exist: " + end_marker);
  return source.substr(begin, end - begin);
}

SynthesizedDispatchPlan identity_dispatch_plan() {
  SynthesizedDispatchPlan plan;
  plan.indirect_register = "9";
  plan.op_name = "id";
  plan.op_opcode = -1;
  plan.scale = 1.0;
  plan.offset = 0.0;
  for (double value : {3.0, 7.0, 12.0}) {
    const auto target = core::resolve_flow_target(plan.indirect_register, value);
    require(target.has_value(), "identity dispatch proof value should resolve statically");
    plan.proof_constraints.push_back(SynthesizedDispatchProofConstraint{
        .input = value,
        .target = *target,
    });
  }
  return plan;
}

}  // namespace

// Forcing the computed-dispatch rescue (via an artificially tiny budget) must
// never break a program: the discovery fixpoint either converges to a formula
// the static proof gate accepts, or it is rejected and the compiler keeps the
// compare-chain lowering. Either way the result stays implemented and
// error-free. This guards the discovery + lowering plumbing against regressions
// independently of whether the dispatch happens to win on size.
void computed_dispatch_discovery_keeps_program_correct() {
  const std::string source = R"mkpro(
program DispatchProbe {
  state {
    score: counter 0..99 = 0
    sel: counter 0..2 = 0
  }
  loop {
    sel = read()
    match sel {
      0 => bump_a()
      1 => bump_b()
      2 => bump_c()
    }
  }
  fn bump_a() { score = score + 4 }
  fn bump_b() { score = score + 5 }
  fn bump_c() { score = score + 6 }
}
)mkpro";

  CompileOptions baseline_options;
  const CompileResult baseline = compile_source(source, baseline_options);
  require(baseline.implemented, "baseline match program should compile");

  CompileOptions rescued_options;
  rescued_options.budget = 4;  // force the size-rescue path, exercising discovery
  const CompileResult rescued = compile_source(source, rescued_options);
  require(rescued.implemented,
          "forcing computed-dispatch discovery must keep the program implemented");
  require(!has_error_diagnostic(rescued),
          "computed-dispatch discovery must not introduce error diagnostics");
  if (has_optimization(rescued, "computed-dispatch")) {
    require(has_proof(rescued, "computed-dispatch-targets"),
            "selected computed-dispatch must report its static target proof");
    const CandidateReport* selected_formula =
        find_candidate(rescued.candidates, "computed-dispatch-formula");
    require(selected_formula != nullptr && selected_formula->selected,
            "selected computed-dispatch must surface computed-dispatch-formula as selected");
  }

  // Stage 3: with a contractually fixed angle mode, discovery additionally lets
  // the solver offer trig ops (angle_fixed). That path must stay correct too:
  // any trig-backed formula is still static-proof gated, so the result is
  // implemented and error-free whether or not a trig dispatch is chosen.
  const std::string angle_fixed_source = R"mkpro(
program DispatchProbeAngle {
  state {
    expected_mode_only("rad")
    score: counter 0..99 = 0
    sel: counter 0..2 = 0
  }
  loop {
    sel = read()
    match sel {
      0 => bump_a()
      1 => bump_b()
      2 => bump_c()
    }
  }
  fn bump_a() { score = score + 4 }
  fn bump_b() { score = score + 5 }
  fn bump_c() { score = score + 6 }
}
)mkpro";
  CompileOptions angle_options;
  angle_options.budget = 4;
  const CompileResult angle_fixed = compile_source(angle_fixed_source, angle_options);
  require(angle_fixed.implemented,
          "computed-dispatch discovery under expected_mode_only must stay implemented");
  require(!has_error_diagnostic(angle_fixed),
          "trig-eligible dispatch discovery must not introduce error diagnostics");
  if (has_optimization(angle_fixed, "computed-dispatch")) {
    require(has_proof(angle_fixed, "computed-dispatch-targets"),
            "selected trig-eligible computed-dispatch must report its static target proof");
    const CandidateReport* selected_formula =
        find_candidate(angle_fixed.candidates, "computed-dispatch-formula");
    require(selected_formula != nullptr && selected_formula->selected,
            "selected trig-eligible computed-dispatch must surface formula candidate as selected");
  }
}

void guarded_computed_dispatch_default_discovery_keeps_program_correct() {
  const std::string source = R"mkpro(
program GuardedDispatchProbe {
  state {
    score: counter 0..99 = 0
    sel: counter 0..9 = 0
  }
  loop {
    sel = read()
    match sel {
      1 => bump_a()
      4 => bump_b()
      7 => bump_c()
      otherwise => bump_default()
    }
  }
  fn bump_a() { score = score + 1 }
  fn bump_b() { score = score + 2 }
  fn bump_c() { score = score + 3 }
  fn bump_default() { score = score + 9 }
}
)mkpro";

  CompileOptions rescued_options;
  rescued_options.budget = 4;
  const CompileResult rescued = compile_source(source, rescued_options);
  require(rescued.implemented,
          "forcing guarded computed-dispatch discovery must keep the program implemented");
  require(!has_error_diagnostic(rescued),
          "guarded computed-dispatch discovery must not introduce error diagnostics");
  if (has_optimization(rescued, "computed-dispatch")) {
    require(has_proof(rescued, "computed-dispatch-targets"),
            "selected guarded computed-dispatch must report its static target proof");
  }

  CompileOptions analysis_options;
  analysis_options.analysis = true;
  analysis_options.disable_candidate_search = true;
  const CompileResult analysis = compile_source(source, analysis_options);
  const CandidateReport* candidate =
      find_candidate(analysis.candidates, "computed-dispatch-formula");
  require(candidate != nullptr, "analysis should report guarded computed-dispatch opportunity");
  require(candidate->reason.find("guarded computed dispatch") != std::string::npos,
          "default match opportunity should describe the guarded dispatch path");
}

void optimizer_analysis_reports_sign_pack_and_address_formula_opportunities() {
  const std::string source = R"mkpro(
program OpportunityReportProbe {
  state {
    expected_mode_only("grd")
    flag: flag = 0
    phase: counter 1..3 = 1
    shown: counter 1..9 = 1
  }

  loop {
    show(shown)
    command = read()
    match command {
      1 => set_flag()
      otherwise => clear_flag()
    }
    if flag == 1 {
      phase = 1
    }
    else {
      phase = 2
    }
    match phase {
      1 => one()
      2 => two()
      3 => three()
    }
  }

  fn set_flag() { flag = 1 }
  fn clear_flag() { flag = 0 }
  fn one() { halt(1) }
  fn two() { halt(2) }
  fn three() { halt(3) }
}
)mkpro";

  CompileOptions options;
  options.analysis = true;
  options.disable_candidate_search = true;
  const CompileResult result = compile_source(source, options);
  require(result.implemented, "opportunity report probe should compile");
  require(!has_error_diagnostic(result), "opportunity report probe should be diagnostic-free");

  const CandidateReport* sign_pack = find_candidate(result.candidates, "sign-pack-state");
  require(sign_pack != nullptr, "analysis should report a sign-pack state opportunity");
  require(!sign_pack->selected, "sign-pack opportunity must be report-only");
  require(sign_pack->reason.find("flag") != std::string::npos,
          "sign-pack opportunity should name the boolean state");
  require(sign_pack->reason.find("phase") != std::string::npos,
          "sign-pack opportunity should name the hidden positive carrier");

  const CandidateReport* computed_dispatch =
      find_candidate(result.candidates, "computed-dispatch-formula");
  require(computed_dispatch != nullptr,
          "analysis should report an address-formula computed-dispatch opportunity");
  require(!computed_dispatch->selected, "computed-dispatch opportunity must be report-only");
  require(computed_dispatch->reason.find("address formula solver") != std::string::npos,
          "computed-dispatch opportunity should point at the address solver");

  require(capability_status(result, "sign-packed-state") == "considered",
          "sign-packed-state capability should be considered when an opportunity is reported");
  require(capability_status(result, "computed-dispatch") == "considered",
          "computed-dispatch capability should be considered when an opportunity is reported");
}

void optimizer_analysis_reports_decimal_and_multi_sign_pack_opportunities() {
  const std::string source = R"mkpro(
program StatePackingOpportunityProbe {
  state {
    flag_a: flag = 0
    flag_b: flag = 1
    mode: counter 0..3 = 0
    small_c: counter 0..99 = 12
    small_a: counter 0..9 = 3
    small_b: counter 0..9 = 4
    carry_a: counter 1..9 = 1
    carry_b: counter 1..9 = 2
  }

  loop {
    command = read()
    if command == 1 {
      flag_a = 1
    }
    else {
      flag_a = 0
    }
    if command == 2 {
      flag_b = 1
    }
    else {
      flag_b = 0
    }
    if flag_a == 1 {
      small_a = 5
      mode = 3
    }
    else {
      small_a = 6
      mode = 2
    }
    if flag_b == 1 {
      small_b = 7
    }
    else {
      small_b = 8
    }
    halt(mode + small_a + small_b + small_c + carry_a + carry_b)
  }
}
)mkpro";

  CompileOptions options;
  options.analysis = true;
  options.disable_candidate_search = true;
  const CompileResult result = compile_source(source, options);
  require(result.implemented, "state-packing opportunity probe should compile");
  require(!has_error_diagnostic(result),
          "state-packing opportunity probe should be diagnostic-free");

  const CandidateReport* decimal_pack = find_candidate(result.candidates, "decimal-pack-state");
  require(decimal_pack != nullptr,
          "analysis should report a decimal state-packing opportunity");
  require(!decimal_pack->selected, "decimal pack opportunity must be report-only");
  require(decimal_pack->reason.find("small_a") != std::string::npos &&
              decimal_pack->reason.find("small_b") != std::string::npos &&
              decimal_pack->reason.find("small_c") != std::string::npos,
          "decimal pack opportunity should name the compact small fields");

  const CandidateReport* sign_tuple =
      find_candidate(result.candidates, "sign-pack-state-tuple");
  require(sign_tuple != nullptr,
          "analysis should report a multi-sign state-packing opportunity");
  require(!sign_tuple->selected, "multi-sign opportunity must be report-only");
  require(sign_tuple->reason.find("flag_a") != std::string::npos &&
              sign_tuple->reason.find("flag_b") != std::string::npos &&
              sign_tuple->reason.find("carry_a") != std::string::npos &&
              sign_tuple->reason.find("carry_b") != std::string::npos,
          "multi-sign opportunity should pair boolean fields with sign carriers");
  const bool reports_two_sign_range =
      std::any_of(result.candidates.begin(), result.candidates.end(),
                  [](const CandidateReport& candidate) {
                    return candidate.variant == "sign-pack-state-tuple" &&
                           candidate.reason.find("mode") != std::string::npos;
                  });
  require(reports_two_sign_range,
          "multi-sign opportunity should also cover a 0..3 state field");

  require(capability_status(result, "decimal-packed-state") == "considered",
          "decimal-packed-state capability should be considered when an opportunity is reported");
  require(capability_status(result, "sign-packed-state") == "considered",
          "sign-packed-state capability should be considered for multi-sign opportunities");
}

void state_packing_rewrite_options_affect_lowering() {
  const std::string source = R"mkpro(
program SignPackingRewriteProbe {
  state {
    flag_a: flag = 1
    flag_b: flag = 0
    carry_a: counter 1..9 = 3
    carry_b: counter 1..9 = 4
    score: counter 0..99 = 0
  }

  loop {
    if flag_a == 1 {
      score = carry_a
    }
    else {
      score = carry_b
    }
    flag_a = 0
    flag_b = 1 - flag_b
    carry_a++
    carry_b--
    halt(score + carry_a + carry_b + flag_a + flag_b)
  }
}
)mkpro";

  CompileOptions single_options;
  single_options.sign_pack_state = true;
  single_options.sign_packed_state_plans = {SignPackedStatePlan{.state = "flag_a",
                                                                .carrier = "carry_a"}};
  const CompileResult single = compile_source(source, single_options);
  require(single.implemented, "forced sign-pack-state rewrite should compile");
  require(!has_error_diagnostic(single),
          "forced sign-pack-state rewrite should be diagnostic-free");
  require(has_optimization(single, "sign-pack-state"),
          "forced sign-pack-state should report a real rewrite optimization");
  require(!single.registers.contains("flag_a"),
          "sign-pack-state rewrite should remove the packed boolean register");
  require(single.registers.contains("carry_a"),
          "sign-pack-state rewrite should keep the sign carrier register");
  const CandidateReport* selected_single = find_candidate(single.candidates, "sign-pack-state");
  require(selected_single != nullptr && selected_single->selected,
          "sign-pack-state should surface as a selected candidate after rewrite");

  CompileOptions tuple_options;
  tuple_options.sign_pack_state = true;
  tuple_options.sign_packed_state_plans = {
      SignPackedStatePlan{.state = "flag_a", .carrier = "carry_a"},
      SignPackedStatePlan{.state = "flag_b", .carrier = "carry_b"},
  };
  const CompileResult tuple = compile_source(source, tuple_options);
  require(tuple.implemented, "forced sign-pack-state-tuple rewrite should compile");
  require(!has_error_diagnostic(tuple),
          "forced sign-pack-state-tuple rewrite should be diagnostic-free");
  require(has_optimization(tuple, "sign-pack-state-tuple"),
          "forced sign-pack-state-tuple should report a real rewrite optimization");
  require(!tuple.registers.contains("flag_a") && !tuple.registers.contains("flag_b"),
          "sign-pack-state-tuple rewrite should remove packed boolean registers");
  require(tuple.registers.contains("carry_a") && tuple.registers.contains("carry_b"),
          "sign-pack-state-tuple rewrite should keep both sign carrier registers");
  const CandidateReport* selected_tuple =
      find_candidate(tuple.candidates, "sign-pack-state-tuple");
  require(selected_tuple != nullptr && selected_tuple->selected,
          "sign-pack-state-tuple should surface as a selected candidate after rewrite");

  const std::string two_bit_source = R"mkpro(
program TwoBitSignPackingRewriteProbe {
  state {
    mode: counter 0..3 = 3
    carry_a: counter 1..9 = 3
    carry_b: counter 1..9 = 4
    score: counter 0..99 = 0
  }

  loop {
    if mode == 3 {
      score = carry_a
    }
    else {
      score = carry_b
    }
    match score {
      0 => mode = 3
      otherwise => mode = 1
    }
    carry_a++
    carry_b--
    halt(score + mode + carry_a + carry_b)
  }
}
)mkpro";

  CompileOptions two_bit_options;
  two_bit_options.sign_pack_state = true;
  two_bit_options.sign_packed_state_plans = {
      SignPackedStatePlan{.state = "mode", .carrier = "carry_a", .bit = 0},
      SignPackedStatePlan{.state = "mode", .carrier = "carry_b", .bit = 1},
  };
  const CompileResult two_bit = compile_source(two_bit_source, two_bit_options);
  require(two_bit.implemented, "forced two-bit sign-pack-state-tuple rewrite should compile");
  require(!has_error_diagnostic(two_bit),
          "forced two-bit sign-pack-state-tuple rewrite should be diagnostic-free");
  require(has_optimization(two_bit, "sign-pack-state-tuple"),
          "forced two-bit sign-pack-state-tuple should report a real rewrite optimization");
  require(!two_bit.registers.contains("mode"),
          "two-bit sign-pack-state-tuple rewrite should remove the 0..3 state register");
  require(two_bit.registers.contains("carry_a") && two_bit.registers.contains("carry_b"),
          "two-bit sign-pack-state-tuple rewrite should keep both sign carrier registers");
}

void optimizer_static_proof_gate_rejects_unproved_dangerous_candidates() {
  CompileOptions computed_options;
  computed_options.synthesized_dispatch_plans.push_back(identity_dispatch_plan());
  CompileResult computed_result;
  computed_result.optimizations.push_back(optimization_report("computed-dispatch"));
  computed_result.steps.push_back(
      resolved_step(0x89,
                    computed_dispatch_comment(computed_options.synthesized_dispatch_plans.at(0),
                                              {"case_a", "case_b", "case_c"})));
  CompileOptions unproved_computed_options = computed_options;
  unproved_computed_options.synthesized_dispatch_plans.at(0).proof_constraints.clear();
  require(!optimizer_static_proof_gate_accepts_for_testing(unproved_computed_options,
                                                           computed_result),
          "computed-dispatch candidate without proof constraints must be rejected");
  require(optimizer_static_proof_gate_accepts_for_testing(computed_options, computed_result),
          "computed-dispatch candidate should be accepted by verified constraints and final artifact without a proof report");

  CompileResult computed_missing_artifact_result = computed_result;
  computed_missing_artifact_result.steps.clear();
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_missing_artifact_result),
          "computed-dispatch candidate without final jump artifact must be rejected");

  CompileResult computed_bad_artifact_result = computed_result;
  computed_bad_artifact_result.steps.clear();
  computed_bad_artifact_result.steps.push_back(
      resolved_step(0x83,
                    computed_dispatch_comment(computed_options.synthesized_dispatch_plans.at(0),
                                              {"case_a", "case_b", "case_c"})));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_bad_artifact_result),
          "computed-dispatch candidate through an unstable final register must be rejected");

  CompileResult computed_wrong_label_count_result = computed_result;
  computed_wrong_label_count_result.steps.clear();
  computed_wrong_label_count_result.steps.push_back(
      resolved_step(0x89,
                    computed_dispatch_comment(computed_options.synthesized_dispatch_plans.at(0),
                                              {"case_a", "case_b"})));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_wrong_label_count_result),
          "computed-dispatch final artifact must cover the same number of targets as the proof constraints");

  CompileResult computed_malformed_labels_result = computed_result;
  computed_malformed_labels_result.steps.clear();
  std::string malformed_computed_labels_comment =
      computed_dispatch_comment(computed_options.synthesized_dispatch_plans.at(0),
                                {"case_a", "case_b", "case_c"});
  malformed_computed_labels_comment.insert(
      malformed_computed_labels_comment.find("; computed-dispatch-proof-targets"), ",");
  computed_malformed_labels_result.steps.push_back(
      resolved_step(0x89, malformed_computed_labels_comment));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_malformed_labels_result),
          "computed-dispatch final artifact must reject malformed target label markers");

  CompileResult computed_embedded_marker_result = computed_result;
  computed_embedded_marker_result.steps.clear();
  computed_embedded_marker_result.steps.push_back(
      resolved_step(0x89, "note: " +
                              computed_dispatch_comment(
                                  computed_options.synthesized_dispatch_plans.at(0),
                                  {"case_a", "case_b", "case_c"})));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_embedded_marker_result),
          "computed-dispatch final artifact must reject embedded proof markers without the "
          "computed-dispatch prefix");

  CompileResult computed_missing_marker_result = computed_result;
  computed_missing_marker_result.steps.insert(computed_missing_marker_result.steps.begin(),
                                              resolved_step(0x89, "computed dispatch; missing proof metadata"));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_missing_marker_result),
          "computed-dispatch final artifact must reject malformed computed-dispatch prefix "
          "comments even when another dispatch artifact is proved");

  CompileResult computed_wrong_proof_targets_result = computed_result;
  computed_wrong_proof_targets_result.steps.clear();
  computed_wrong_proof_targets_result.steps.push_back(
      resolved_step(0x89,
                    "computed dispatch; computed-dispatch-targets=case_a,case_b,case_c; "
                    "computed-dispatch-proof-targets=104,103,102"));
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_options,
                                                           computed_wrong_proof_targets_result),
          "computed-dispatch final artifact must carry proof targets matching the solved "
          "constraints");

  computed_result.proofs.push_back(proved_report("computed-dispatch-targets"));
  require(!optimizer_static_proof_gate_accepts_for_testing(unproved_computed_options,
                                                           computed_result),
          "computed-dispatch candidate must not be accepted by a fabricated proof report");
  require(optimizer_static_proof_gate_accepts_for_testing(computed_options, computed_result),
          "computed-dispatch candidate with matching proof constraints should be accepted");

  CompileOptions computed_with_fractional_options = computed_options;
  computed_with_fractional_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = 3});
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_with_fractional_options,
                                                           computed_result),
          "computed-dispatch gate must not accept mixed fractional-selector candidates");

  CompileOptions computed_with_dead_integer_options = computed_options;
  computed_with_dead_integer_options.assume_dead_selector_integer_part = true;
  require(!optimizer_static_proof_gate_accepts_for_testing(computed_with_dead_integer_options,
                                                           computed_result),
          "computed-dispatch gate must not accept mixed dead-integer candidates");

  CompileOptions stale_computed_options = computed_options;
  stale_computed_options.synthesized_dispatch_plans.at(0).offset = 1.0;
  require(!optimizer_static_proof_gate_accepts_for_testing(stale_computed_options,
                                                           computed_result),
          "computed-dispatch candidate with stale formula constraints must be rejected");

  CompileOptions preloaded_options;
  preloaded_options.preloaded_indirect_flow = true;
  CompileResult preloaded_result;
  preloaded_result.optimizations.push_back(optimization_report("preloaded-indirect-flow"));
  require(!optimizer_static_proof_gate_accepts_for_testing(preloaded_options, preloaded_result),
          "preloaded indirect-flow candidate without target proof must be rejected");

  CompileResult fabricated_preloaded_report = preloaded_result;
  fabricated_preloaded_report.proofs.push_back(proved_report("indirect-flow-targets"));
  require(!optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                           fabricated_preloaded_report),
          "preloaded indirect-flow candidate must not be accepted by a fabricated proof report");

  const int target = *core::resolve_flow_target("9", 3.0);
  CompileResult preloaded_mismatched_annotation_result = preloaded_result;
  preloaded_mismatched_annotation_result.preloads.push_back(flow_preload("9", "3"));
  preloaded_mismatched_annotation_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "4", target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_mismatched_annotation_result),
          "preloaded indirect-flow candidate must reject selector values that disagree with the "
          "rewrite annotation");

  CompileResult preloaded_bad_target_marker_result = preloaded_result;
  preloaded_bad_target_marker_result.preloads.push_back(flow_preload("9", "3"));
  preloaded_bad_target_marker_result.steps.push_back(
      resolved_step(0x89, "preloaded R9=3 indirect-target=" + std::to_string(target) +
                              "garbage indirect flow"));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_bad_target_marker_result),
          "preloaded indirect-flow proof must reject malformed indirect-target markers");

  CompileResult preloaded_reordered_annotation_result = preloaded_result;
  preloaded_reordered_annotation_result.preloads.push_back(flow_preload("9", "3"));
  preloaded_reordered_annotation_result.steps.push_back(
      resolved_step(0x89, "indirect-target=" + std::to_string(target) +
                              " preloaded R9=3 indirect flow"));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_reordered_annotation_result),
          "preloaded indirect-flow proof must reject target markers that precede selector values");

  preloaded_result.preloads.push_back(flow_preload("9", "3"));
  preloaded_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "3", target)));
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options, preloaded_result),
          "preloaded indirect-flow candidate with verified target artifact should be accepted");

  CompileResult preloaded_with_raw_operand_result = preloaded_result;
  preloaded_with_raw_operand_result.steps.insert(preloaded_with_raw_operand_result.steps.begin(),
                                                resolved_step_with_mnemonic(
                                                    0x49, "49", "raw address operand"));
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_with_raw_operand_result),
          "preloaded indirect-flow proof must not treat raw operand bytes as register writes");

  CompileResult artifact_only_preloaded_result = preloaded_result;
  artifact_only_preloaded_result.optimizations.clear();
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          artifact_only_preloaded_result),
          "preloaded indirect-flow proof should be accepted from final artifacts without trusting "
          "optimization report names");

  CompileResult preloaded_dead_allocated_selector_result = preloaded_result;
  preloaded_dead_allocated_selector_result.registers["dead_value"] = "9";
  require(optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_dead_allocated_selector_result),
          "preloaded indirect-flow proof should allow allocated selector registers that are not "
          "accessed as data by the final program");

  CompileResult preloaded_read_allocated_selector_result = preloaded_result;
  preloaded_read_allocated_selector_result.registers["live_value"] = "9";
  preloaded_read_allocated_selector_result.steps.insert(
      preloaded_read_allocated_selector_result.steps.begin(),
      resolved_step_with_mnemonic(0x69, "П->X 9", "recall live_value"));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_read_allocated_selector_result),
          "preloaded indirect-flow proof must reject allocated selector registers that are read as "
          "data by the final program");

  CompileResult preloaded_written_selector_result = preloaded_result;
  preloaded_written_selector_result.registers["live_value"] = "9";
  preloaded_written_selector_result.steps.insert(preloaded_written_selector_result.steps.begin(),
                                                resolved_step_with_mnemonic(0x49, "X->П 9",
                                                                            "set live_value"));
  require(!optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                           preloaded_written_selector_result),
          "preloaded indirect-flow proof must reject selector preloads that are overwritten by the "
          "final program");

  CompileResult preloaded_compiler_selector_result = preloaded_result;
  preloaded_compiler_selector_result.registers["__loop_indirect"] = "9";
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_compiler_selector_result),
          "preloaded indirect-flow proof should allow compiler-owned selector allocations");

  CompileResult preloaded_constant_selector_result = preloaded_result;
  preloaded_constant_selector_result.registers["__const_3"] = "9";
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_constant_selector_result),
          "preloaded indirect-flow proof should allow compiler-owned constant selector reuse");

  const auto bit_mask_selector_target =
      core::evaluate_indirect_address("e", "58", core::IndirectOperationKind::Flow);
  require(bit_mask_selector_target.has_value(), "bit-mask selector target should resolve");
  require(bit_mask_selector_target->actual_flow_target.has_value(),
          "bit-mask selector target should resolve to a final flow address");
  CompileResult preloaded_bit_mask_selector_result;
  preloaded_bit_mask_selector_result.optimizations.push_back(
      optimization_report("preloaded-indirect-flow"));
  preloaded_bit_mask_selector_result.preloads.push_back(flow_preload("e", "58"));
  preloaded_bit_mask_selector_result.steps.push_back(
      resolved_step(0xae, indirect_target_comment("e", "58",
                                                  *bit_mask_selector_target->actual_flow_target)));
  preloaded_bit_mask_selector_result.registers["__bit_mask_71"] = "e";
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_bit_mask_selector_result),
          "preloaded indirect-flow proof should allow compiler-owned bit-mask selector reuse");

  CompileOptions options_only_preload_options = preloaded_options;
  options_only_preload_options.preloaded_constant_registers["9"] = "3";
  CompileResult options_only_preload_result;
  options_only_preload_result.optimizations.push_back(
      optimization_report("preloaded-indirect-flow"));
  options_only_preload_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "3", target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(options_only_preload_options,
                                                           options_only_preload_result),
          "preloaded indirect-flow proof must reject selector values that exist only in compile "
          "options and not in final preload artifacts");

  const auto formal_alias_target =
      core::evaluate_indirect_address("9", "B2", core::IndirectOperationKind::Flow);
  require(formal_alias_target.has_value(), "formal selector alias B2 should resolve statically");
  require(formal_alias_target->actual_flow_target.has_value(),
          "formal selector alias B2 should resolve to a flow target");
  CompileResult preloaded_formal_alias_result;
  preloaded_formal_alias_result.optimizations.push_back(
      optimization_report("preloaded-indirect-flow"));
  preloaded_formal_alias_result.preloads.push_back(flow_preload("9", "B2"));
  preloaded_formal_alias_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "b2",
                                                  *formal_alias_target->actual_flow_target)));
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_formal_alias_result),
          "preloaded indirect-flow proof should accept case-insensitive formal aliases");

  CompileResult preloaded_formal_alias_mismatch_result = preloaded_formal_alias_result;
  preloaded_formal_alias_mismatch_result.steps.clear();
  preloaded_formal_alias_mismatch_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "2",
                                                  *formal_alias_target->actual_flow_target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_formal_alias_mismatch_result),
          "preloaded indirect-flow proof must not numeric-normalize formal aliases");

  CompileResult preloaded_with_runtime_marker_result = preloaded_result;
  preloaded_with_runtime_marker_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(optimizer_static_proof_gate_accepts_for_testing(preloaded_options,
                                                          preloaded_with_runtime_marker_result),
          "preloaded indirect-flow proof should ignore runtime-specific indirect-call markers");

  CompileResult preloaded_with_unstable_target_result = preloaded_result;
  preloaded_with_unstable_target_result.steps.push_back(
      resolved_step(0x83, indirect_target_comment("3", "3", target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              preloaded_options, preloaded_with_unstable_target_result),
          "preloaded indirect-flow proof must reject annotated targets through unstable registers");

  CompileOptions suppressed_preload_options = preloaded_options;
  suppressed_preload_options.suppress_constant_preloads.insert("10000");
  CompileResult bad_suppressed_preload_result = preloaded_result;
  bad_suppressed_preload_result.preloads.push_back(flow_preload("8", "10000"));
  require(!optimizer_static_proof_gate_accepts_for_testing(suppressed_preload_options,
                                                           bad_suppressed_preload_result),
          "suppressed-preload indirect-flow candidate must reject final preloads that keep the "
          "suppressed value");

  require(optimizer_static_proof_gate_accepts_for_testing(suppressed_preload_options,
                                                          preloaded_result),
          "suppressed-preload indirect-flow candidate should be accepted when the suppressed value "
          "is absent from final preloads");

  CompileOptions suppress_only_options;
  suppress_only_options.suppress_constant_preloads.insert("10000");
  CompileResult bad_suppress_only_result;
  bad_suppress_only_result.preloads.push_back(flow_preload("7", "10000"));
  require(!optimizer_static_proof_gate_accepts_for_testing(suppress_only_options,
                                                           bad_suppress_only_result),
          "suppress-only candidate must reject final preloads that keep the suppressed value");

  CompileResult suppress_only_result;
  suppress_only_result.preloads.push_back(flow_preload("7", "20000"));
  require(optimizer_static_proof_gate_accepts_for_testing(suppress_only_options,
                                                          suppress_only_result),
          "suppress-only candidate should be accepted when the suppressed value is absent from "
          "final preloads");

  CompileOptions forward_options;
  forward_options.forward_indirect_flow = true;
  CompileResult forward_result;
  forward_result.optimizations.push_back(optimization_report("preloaded-indirect-flow"));
  require(!optimizer_static_proof_gate_accepts_for_testing(forward_options, forward_result),
          "forward indirect-flow candidate without target proof must be rejected");

  forward_result.preloads.push_back(flow_preload("9", "3"));
  forward_result.steps.push_back(resolved_step(0x89, indirect_target_comment("9", "3", target)));
  require(optimizer_static_proof_gate_accepts_for_testing(forward_options, forward_result),
          "forward indirect-flow candidate with verified target artifact should be accepted");

  CompileOptions fox_aggressive_options;
  fox_aggressive_options.analysis = true;
  fox_aggressive_options.aggressive_post_layout_indirect_flow = true;
  const CompileResult fox_aggressive_result = compile_source(
      read_text_file(project_file("examples/fox-hunt-mk61.mkpro")), fox_aggressive_options);
  require(fox_aggressive_result.implemented,
          "fox-hunt aggressive post-layout indirect-flow candidate should compile");
  require(fox_aggressive_result.steps.size() == 60,
          "fox-hunt aggressive post-layout indirect-flow candidate should keep the measured "
          "60-cell result");
  const std::optional<std::string> fox_aggressive_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(fox_aggressive_options,
                                                               fox_aggressive_result);
  require(fox_aggressive_rejection.has_value() &&
              fox_aggressive_rejection->find("proofFamily=indirect-flow-targets") !=
                  std::string::npos &&
              fox_aggressive_rejection->find("missingProof=selector-register-preservation") !=
                  std::string::npos &&
              fox_aggressive_rejection->find("proofFailure=selector-register-used-as-data") !=
                  std::string::npos &&
              fox_aggressive_rejection->find(
                  "selectorDataConflictResolutionStatus="
                  "proved-selector-data-overlap-requires-payload-repacking") !=
                  std::string::npos &&
              fox_aggressive_rejection->find(
                  "proofDisposition=proved-conflict-needs-layout-change") !=
                  std::string::npos &&
              fox_aggressive_rejection->find(
                  "selectorDataPayloadLayout=contiguous-indirect-window") !=
                  std::string::npos &&
              fox_aggressive_rejection->find(
                  "selectorDataPayloadTargetRange=6..e") != std::string::npos &&
              fox_aggressive_rejection->find(
                  "selectorDataContiguousRelocationStatus="
                  "no-selector-free-contiguous-window") != std::string::npos &&
              fox_aggressive_rejection->find(
                  "selectorDataPayloadPackingRequirement="
                  "pack-or-split-contiguous-indirect-payload") !=
                  std::string::npos &&
              fox_aggressive_rejection->find("requiredAction=pack-data-away-from-flow-selectors") !=
                  std::string::npos,
          "fox-hunt aggressive post-layout indirect-flow rejection should explain the selector "
          "register/data payload-layout blocker");
  require(!optimizer_static_proof_gate_accepts_for_testing(fox_aggressive_options,
                                                           fox_aggressive_result),
          "fox-hunt aggressive post-layout indirect-flow candidate must stay rejected while "
          "selector registers can still be read as coord_list data");

  CompileOptions forward_dead_integer_options = forward_options;
  forward_dead_integer_options.assume_dead_selector_integer_part = true;
  require(!optimizer_static_proof_gate_accepts_for_testing(forward_dead_integer_options,
                                                           forward_result),
          "forward indirect-flow candidate must not bypass the dead-integer proof requirement");

  CompileOptions pure_dead_integer_options;
  pure_dead_integer_options.assume_dead_selector_integer_part = true;
  CompileResult pure_dead_integer_result;
  pure_dead_integer_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  pure_dead_integer_result.steps = forward_result.steps;
  pure_dead_integer_result.preloads = forward_result.preloads;
  require(!optimizer_static_proof_gate_accepts_for_testing(pure_dead_integer_options,
                                                           pure_dead_integer_result),
          "dead-integer fractional selector elision must stay rejected without its own proof");

  CompileOptions forward_fractional_options = forward_options;
  forward_fractional_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = target});

  CompileOptions safe_dead_integer_options = forward_fractional_options;
  safe_dead_integer_options.assume_dead_selector_integer_part = true;
  CompileResult safe_dead_integer_result = forward_result;
  safe_dead_integer_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_dead_integer_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_dead_integer_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_dead_integer_result),
          "dead-integer fractional selector elision should be accepted when final artifacts "
          "immediately erase the retuned integer part");
  require(!optimizer_static_proof_gate_rejection_reason_for_testing(
               safe_dead_integer_options, safe_dead_integer_result)
               .has_value(),
          "accepted dead-integer fractional selector elision should not report a rejection reason");

  const std::optional<int> direct_dead_integer_target =
      core::resolve_flow_target("9", 3.123456);
  require(direct_dead_integer_target.has_value(),
          "dead-integer selector target should resolve statically");
  CompileOptions direct_dead_integer_options = forward_options;
  direct_dead_integer_options.assume_dead_selector_integer_part = true;
  direct_dead_integer_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456",
                                     .target = *direct_dead_integer_target});
  CompileResult direct_dead_integer_jump_result;
  direct_dead_integer_jump_result.optimizations.push_back(
      optimization_report("preloaded-indirect-flow"));
  direct_dead_integer_jump_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  direct_dead_integer_jump_result.preloads.push_back(flow_preload("9", "3.123456"));
  direct_dead_integer_jump_result.steps.push_back(resolved_step_with_mnemonic(
      0x69, "П->X 9", "preload const 3.123456; fractional selector source 0.123456"));
  direct_dead_integer_jump_result.steps.push_back(resolved_step_with_mnemonic(
      0x89, "К БП 9", indirect_target_comment("9", "3.123456",
                                               *direct_dead_integer_target)));
  require(optimizer_static_proof_gate_accepts_for_testing(direct_dead_integer_options,
                                                          direct_dead_integer_jump_result),
          "dead-integer fractional selector elision should accept a proved direct indirect jump "
          "that consumes the integer part only as an address");

  int conditional_address = 11;
  while (conditional_address == *direct_dead_integer_target ||
         conditional_address + 1 == *direct_dead_integer_target) {
    ++conditional_address;
  }
  const int conditional_fallthrough_address = conditional_address + 1;
  int conditional_recall_address = 90;
  while (conditional_recall_address == conditional_address ||
         conditional_recall_address == conditional_fallthrough_address ||
         conditional_recall_address == *direct_dead_integer_target) {
    --conditional_recall_address;
  }
  CompileResult direct_dead_integer_conditional_result = direct_dead_integer_jump_result;
  direct_dead_integer_conditional_result.steps = {
      resolved_step_with_mnemonic_at(
          conditional_recall_address, 0x69, "П->X 9",
          "preload const 3.123456; fractional selector source 0.123456"),
      resolved_step_with_mnemonic_at(
          conditional_address, 0xe9, "К x=0 9",
          indirect_target_comment("9", "3.123456", *direct_dead_integer_target)),
      resolved_step_at(conditional_fallthrough_address, 0x35, "frac fallthrough"),
      resolved_step_at(*direct_dead_integer_target, 0x35, "frac branch"),
  };
  require(optimizer_static_proof_gate_accepts_for_testing(
              direct_dead_integer_options, direct_dead_integer_conditional_result),
          "dead-integer fractional selector elision should accept a proved conditional indirect "
          "flow when both branch and fallthrough immediately erase the live X carrier");

  CompileResult unproved_dead_integer_conditional_result =
      direct_dead_integer_conditional_result;
  unproved_dead_integer_conditional_result.steps.back() =
      resolved_step_at(*direct_dead_integer_target, 0x10, "branch consumes live x");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              direct_dead_integer_options, unproved_dead_integer_conditional_result),
          "dead-integer fractional selector elision must reject conditional indirect flow when "
          "one successor can consume the live X carrier before K {x}");
  const std::optional<std::string> unproved_dead_integer_conditional_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          direct_dead_integer_options, unproved_dead_integer_conditional_result);
  require(unproved_dead_integer_conditional_reason.has_value() &&
              unproved_dead_integer_conditional_reason->find(
                  "xLivenessProofScope=conditional-indirect-flow") != std::string::npos &&
              unproved_dead_integer_conditional_reason->find("branchTarget=") !=
                  std::string::npos &&
              unproved_dead_integer_conditional_reason->find("fallthroughSite=") !=
                  std::string::npos,
          "dead-integer conditional-flow rejection should expose the two-successor X-liveness "
          "proof gap");

  CompileResult direct_dead_integer_memory_result = safe_dead_integer_result;
  direct_dead_integer_memory_result.steps.back() = resolved_step_with_mnemonic(
      0xd9, "К П->X 9", "indexed recall cells; indirect-memory-targets=3");
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          direct_dead_integer_memory_result),
          "dead-integer fractional selector elision should accept a proved direct indirect-memory "
          "recall that consumes the integer part only as an address");

  CompileResult mismatched_dead_integer_memory_result = safe_dead_integer_result;
  mismatched_dead_integer_memory_result.steps.back() = resolved_step_with_mnemonic(
      0xd9, "К П->X 9", "indexed recall cells; indirect-memory-targets=4");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, mismatched_dead_integer_memory_result),
          "dead-integer fractional selector elision must reject indirect-memory recall targets "
          "that do not match the carrier integer part");
  const std::optional<std::string> mismatched_dead_integer_memory_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, mismatched_dead_integer_memory_result);
  require(mismatched_dead_integer_memory_reason.has_value() &&
              mismatched_dead_integer_memory_reason->find("reaches К П->X 9 before K {x}") !=
                  std::string::npos &&
              mismatched_dead_integer_memory_reason->find("(indirect memory recall use)") !=
                  std::string::npos,
          "dead-integer rejection reason must classify mismatched indirect-memory recalls");

  CompileResult direct_dead_integer_call_result = direct_dead_integer_jump_result;
  direct_dead_integer_call_result.steps.back() = resolved_step_with_mnemonic(
      0xa9, "К ПП 9", indirect_target_comment("9", "3.123456",
                                               *direct_dead_integer_target));
  require(!optimizer_static_proof_gate_accepts_for_testing(direct_dead_integer_options,
                                                           direct_dead_integer_call_result),
          "dead-integer fractional selector elision must still reject direct indirect calls "
          "because control returns with the un-erased X value potentially live");
  const std::optional<std::string> direct_dead_integer_call_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(direct_dead_integer_options,
                                                               direct_dead_integer_call_result);
  require(direct_dead_integer_call_reason.has_value() &&
              direct_dead_integer_call_reason->find("xLivenessProofScope=indirect-call-return") !=
                  std::string::npos &&
              direct_dead_integer_call_reason->find("returnSite=01") != std::string::npos &&
              direct_dead_integer_call_reason->find("calleeTarget=") != std::string::npos,
          "dead-integer indirect-call rejection should expose the callee-return X-liveness "
          "proof gap");

  CompileResult safe_direct_dead_integer_call_result = direct_dead_integer_call_result;
  safe_direct_dead_integer_call_result.steps.push_back(
      resolved_step_at(*direct_dead_integer_target, 0x35, "callee erases live selector"));
  require(optimizer_static_proof_gate_accepts_for_testing(direct_dead_integer_options,
                                                          safe_direct_dead_integer_call_result),
          "dead-integer fractional selector elision should accept a proved indirect call whose "
          "callee immediately erases the live X carrier");

  CompileResult safe_stored_dead_integer_result = forward_result;
  safe_stored_dead_integer_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_dead_integer_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_dead_integer_result.steps.push_back(resolved_step(0x49, "set saved selector"));
  safe_stored_dead_integer_result.steps.push_back(resolved_step(0x35, "frac()"));
  safe_stored_dead_integer_result.steps.push_back(resolved_step(0x69, "recall saved selector"));
  safe_stored_dead_integer_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_dead_integer_result),
          "dead-integer fractional selector elision should accept direct store forwarding when "
          "the live X value and every later recall are immediately erased");

  CompileResult safe_stored_address_use_result = safe_stored_dead_integer_result;
  safe_stored_address_use_result.steps.insert(
      safe_stored_address_use_result.steps.end() - 2,
      resolved_step_with_mnemonic(0x89, "К БП 9", indirect_target_comment("9", "3", target)));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_address_use_result),
          "dead-integer fractional selector elision should allow proved stored selector address "
          "uses while still requiring data recalls to be erased");

  CompileResult safe_stored_memory_recall_result = safe_stored_dead_integer_result;
  safe_stored_memory_recall_result.steps.insert(
      safe_stored_memory_recall_result.steps.end() - 2,
      resolved_step_with_mnemonic(0xd9, "К П->X 9",
                                  "indexed recall cells; indirect-memory-targets=3"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_memory_recall_result),
          "dead-integer fractional selector elision should allow proved stored selector "
          "indirect-memory recalls while still requiring data recalls to be erased");

  CompileResult safe_stored_memory_store_result = safe_stored_dead_integer_result;
  safe_stored_memory_store_result.steps.insert(
      safe_stored_memory_store_result.steps.end() - 2,
      resolved_step_with_mnemonic(0xb9, "К X->П 9",
                                  "indexed store cells; indirect-memory-targets=3"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_memory_store_result),
          "dead-integer fractional selector elision should allow proved stored selector "
          "indirect-memory stores while still requiring data recalls to be erased");

  CompileResult safe_stored_indirect_call_result = safe_stored_dead_integer_result;
  safe_stored_indirect_call_result.steps.insert(
      safe_stored_indirect_call_result.steps.end() - 2,
      resolved_step_with_mnemonic(0xa9, "К ПП 9",
                                  indirect_target_comment("9", "3", target)));
  safe_stored_indirect_call_result.steps.insert(
      safe_stored_indirect_call_result.steps.end() - 2,
      resolved_step_at(target, 0x35, "callee erases stored selector"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_indirect_call_result),
          "dead-integer fractional selector elision should allow proved stored selector "
          "indirect calls when the callee immediately erases the live X carrier");

  CompileResult unsafe_stored_indirect_call_result = safe_stored_indirect_call_result;
  unsafe_stored_indirect_call_result.steps.at(
      unsafe_stored_indirect_call_result.steps.size() - 3U) =
      resolved_step_at(target, 0x10, "callee consumes live selector");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, unsafe_stored_indirect_call_result),
          "dead-integer fractional selector elision must reject stored selector indirect calls "
          "when the callee can consume the live X carrier before K {x}");
  const std::optional<std::string> unsafe_stored_indirect_call_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_indirect_call_result);
  require(unsafe_stored_indirect_call_reason.has_value() &&
              unsafe_stored_indirect_call_reason->find("stored in R9 reaches К ПП 9 before "
                                                       "K {x}") != std::string::npos &&
              unsafe_stored_indirect_call_reason->find("xLivenessProofScope=indirect-call-return") !=
                  std::string::npos,
          "dead-integer rejection reason must classify stored-selector indirect-call callee "
          "entry gaps");

  CompileResult mismatched_stored_memory_store_result = safe_stored_memory_store_result;
  mismatched_stored_memory_store_result.steps.at(
      mismatched_stored_memory_store_result.steps.size() - 3U) =
      resolved_step_with_mnemonic(0xb9, "К X->П 9",
                                  "indexed store cells; indirect-memory-targets=4");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, mismatched_stored_memory_store_result),
          "dead-integer fractional selector elision must reject indirect-memory store targets "
          "that do not match the stored carrier integer part");
  const std::optional<std::string> mismatched_stored_memory_store_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, mismatched_stored_memory_store_result);
  require(mismatched_stored_memory_store_reason.has_value() &&
              mismatched_stored_memory_store_reason->find("stored in R9 reaches К X->П 9 "
                                                          "before K {x}") != std::string::npos &&
              mismatched_stored_memory_store_reason->find("(indirect memory store use)") !=
                  std::string::npos,
          "dead-integer rejection reason must classify mismatched indirect-memory stores");

  CompileResult safe_stored_immediate_memory_recall_result = forward_result;
  safe_stored_immediate_memory_recall_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_immediate_memory_recall_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_immediate_memory_recall_result.steps.push_back(
      resolved_step(0x49, "set saved selector"));
  safe_stored_immediate_memory_recall_result.steps.push_back(resolved_step_with_mnemonic(
      0xd9, "К П->X 9", "indexed recall cells; indirect-memory-targets=3"));
  safe_stored_immediate_memory_recall_result.steps.push_back(
      resolved_step(0x69, "recall saved selector"));
  safe_stored_immediate_memory_recall_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, safe_stored_immediate_memory_recall_result),
          "dead-integer fractional selector elision should accept a stored selector that is "
          "immediately consumed by a proved indirect-memory recall because that overwrites live X");

  CompileResult safe_stored_immediate_direct_recall_result = forward_result;
  safe_stored_immediate_direct_recall_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_immediate_direct_recall_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_immediate_direct_recall_result.steps.push_back(
      resolved_step(0x49, "set saved selector"));
  safe_stored_immediate_direct_recall_result.steps.push_back(
      resolved_step_with_mnemonic(0x60, "П->X 0", "overwrite live selector"));
  safe_stored_immediate_direct_recall_result.steps.push_back(
      resolved_step(0x69, "recall saved selector"));
  safe_stored_immediate_direct_recall_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, safe_stored_immediate_direct_recall_result),
          "dead-integer fractional selector elision should accept a stored selector when a direct "
          "recall from another register overwrites the still-live X carrier");

  CompileResult safe_stored_immediate_same_recall_result = forward_result;
  safe_stored_immediate_same_recall_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_immediate_same_recall_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_immediate_same_recall_result.steps.push_back(
      resolved_step(0x49, "set saved selector"));
  safe_stored_immediate_same_recall_result.steps.push_back(
      resolved_step_with_mnemonic(0x69, "П->X 9", "recall saved selector"));
  safe_stored_immediate_same_recall_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, safe_stored_immediate_same_recall_result),
          "dead-integer fractional selector elision should accept a stored selector immediately "
          "recalled from the same register only when K {x} erases it");

  CompileResult unsafe_stored_immediate_same_recall_result =
      safe_stored_immediate_same_recall_result;
  unsafe_stored_immediate_same_recall_result.steps.back() =
      resolved_step(0x12, "expr *");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, unsafe_stored_immediate_same_recall_result),
          "dead-integer fractional selector elision must reject a stored selector immediately "
          "recalled from the same register when it is consumed before K {x}");
  const std::optional<std::string> unsafe_stored_immediate_same_recall_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_immediate_same_recall_result);
  require(unsafe_stored_immediate_same_recall_reason.has_value() &&
              unsafe_stored_immediate_same_recall_reason->find(
                  "is stored before П->X 9 instead of immediate K {x}") != std::string::npos,
          "dead-integer rejection reason should keep distinguishing unsafe same-register recalls "
          "after a selector store");

  int stored_conditional_address = 70;
  while (stored_conditional_address == target || stored_conditional_address + 1 == target)
    ++stored_conditional_address;
  CompileResult safe_stored_immediate_conditional_result = forward_result;
  safe_stored_immediate_conditional_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_immediate_conditional_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_immediate_conditional_result.steps.push_back(
      resolved_step(0x49, "set saved selector"));
  safe_stored_immediate_conditional_result.steps.push_back(resolved_step_with_mnemonic_at(
      stored_conditional_address, 0xe9, "К x=0 9",
      indirect_target_comment("9", "3", target)));
  safe_stored_immediate_conditional_result.steps.push_back(
      resolved_step_at(stored_conditional_address + 1, 0x35, "frac fallthrough"));
  safe_stored_immediate_conditional_result.steps.push_back(
      resolved_step_at(target, 0x35, "frac branch"));
  safe_stored_immediate_conditional_result.steps.push_back(
      resolved_step(0x69, "recall saved selector"));
  safe_stored_immediate_conditional_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, safe_stored_immediate_conditional_result),
          "dead-integer fractional selector elision should accept a stored selector immediately "
          "used by proved conditional indirect flow when both successors erase live X");

  CompileResult unsafe_stored_immediate_conditional_result =
      safe_stored_immediate_conditional_result;
  unsafe_stored_immediate_conditional_result.steps.at(
      unsafe_stored_immediate_conditional_result.steps.size() - 3U) =
      resolved_step_at(target, 0x10, "branch consumes live selector");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, unsafe_stored_immediate_conditional_result),
          "dead-integer fractional selector elision must reject immediate stored-selector "
          "conditional flow when one successor can consume the live X carrier");
  const std::optional<std::string> unsafe_stored_immediate_conditional_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_immediate_conditional_result);
  require(unsafe_stored_immediate_conditional_reason.has_value() &&
              unsafe_stored_immediate_conditional_reason->find(
                  "is stored before К x=0 9 instead of immediate K {x}") != std::string::npos &&
              unsafe_stored_immediate_conditional_reason->find(
                  "xLivenessProofScope=conditional-indirect-flow") != std::string::npos,
          "dead-integer stored conditional-flow rejection should expose the successor erasure "
          "proof gap");

  CompileResult safe_stored_immediate_call_result = forward_result;
  safe_stored_immediate_call_result.optimizations.push_back(
      optimization_report("dead-integer-fractional-selector-use"));
  safe_stored_immediate_call_result.steps.push_back(
      resolved_step(0x69, "preload const 3.123456; fractional selector source 0.123456"));
  safe_stored_immediate_call_result.steps.push_back(resolved_step(0x49, "set saved selector"));
  safe_stored_immediate_call_result.steps.push_back(resolved_step_with_mnemonic(
      0xa9, "К ПП 9", indirect_target_comment("9", "3", target)));
  safe_stored_immediate_call_result.steps.push_back(
      resolved_step_at(target, 0x35, "callee erases stored selector"));
  safe_stored_immediate_call_result.steps.push_back(
      resolved_step(0x69, "recall saved selector"));
  safe_stored_immediate_call_result.steps.push_back(resolved_step(0x35, "frac()"));
  require(optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                          safe_stored_immediate_call_result),
          "dead-integer fractional selector elision should accept a stored selector immediately "
          "used by a proved indirect call whose callee entry erases live X");

  CompileResult unsafe_stored_immediate_call_result = safe_stored_immediate_call_result;
  unsafe_stored_immediate_call_result.steps.at(
      unsafe_stored_immediate_call_result.steps.size() - 3U) =
      resolved_step_at(target, 0x10, "callee consumes live selector");
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_stored_immediate_call_result),
          "dead-integer fractional selector elision must reject immediate stored-selector "
          "indirect calls when the callee can consume live X before K {x}");
  const std::optional<std::string> unsafe_stored_immediate_call_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_immediate_call_result);
  require(unsafe_stored_immediate_call_reason.has_value() &&
              unsafe_stored_immediate_call_reason->find(
                  "is stored before К ПП 9 instead of immediate K {x}") != std::string::npos &&
              unsafe_stored_immediate_call_reason->find(
                  "xLivenessProofScope=indirect-call-return") != std::string::npos,
          "dead-integer stored indirect-call rejection should expose the callee-entry erasure "
          "proof gap");

  CompileResult unsafe_stored_immediate_memory_store_result =
      safe_stored_immediate_memory_recall_result;
  unsafe_stored_immediate_memory_store_result.steps.at(
      unsafe_stored_immediate_memory_store_result.steps.size() - 3U) =
      resolved_step_with_mnemonic(0xb9, "К X->П 9",
                                  "indexed store cells; indirect-memory-targets=3");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, unsafe_stored_immediate_memory_store_result),
          "dead-integer fractional selector elision must reject a stored selector immediately "
          "used by an indirect-memory store because the live X carrier would be stored");
  const std::optional<std::string> unsafe_stored_immediate_memory_store_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_immediate_memory_store_result);
  require(unsafe_stored_immediate_memory_store_reason.has_value() &&
              unsafe_stored_immediate_memory_store_reason->find(
                  "is stored before К X->П 9 instead of immediate K {x}") != std::string::npos,
          "dead-integer rejection reason should keep distinguishing unsafe live-X memory stores "
          "after a selector store");
  require(unsafe_stored_immediate_memory_store_reason->find("(indirect memory store use)") !=
              std::string::npos,
          "dead-integer rejection reason should classify unsafe live-X memory stores after a "
          "selector store");

  CompileResult unsafe_stored_immediate_jump_result = safe_stored_immediate_memory_recall_result;
  unsafe_stored_immediate_jump_result.steps.at(unsafe_stored_immediate_jump_result.steps.size() -
                                               3U) =
      resolved_step_with_mnemonic(0x89, "К БП 9", indirect_target_comment("9", "3", target));
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_stored_immediate_jump_result),
          "dead-integer fractional selector elision must reject a stored selector immediately "
          "used by indirect flow because the live X carrier is not overwritten");
  const std::optional<std::string> unsafe_stored_immediate_jump_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_immediate_jump_result);
  require(unsafe_stored_immediate_jump_reason.has_value() &&
              unsafe_stored_immediate_jump_reason->find("is stored before К БП 9 instead of "
                                                        "immediate K {x}") !=
                  std::string::npos,
          "dead-integer rejection reason should keep distinguishing unsafe live-X flow after "
          "a selector store");
  require(unsafe_stored_immediate_jump_reason->find("(indirect address/control use)") !=
              std::string::npos,
          "dead-integer rejection reason should classify unsafe live-X flow after a selector "
          "store");

  CompileResult unsafe_stored_unmarked_address_use_result = safe_stored_dead_integer_result;
  unsafe_stored_unmarked_address_use_result.steps.insert(
      unsafe_stored_unmarked_address_use_result.steps.end() - 2,
      resolved_step_with_mnemonic(0x89, "К БП 9", "stored selector address use"));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              safe_dead_integer_options, unsafe_stored_unmarked_address_use_result),
          "dead-integer fractional selector elision must reject stored selector address uses "
          "without a final indirect-target proof artifact");
  const std::optional<std::string> unsafe_stored_unmarked_address_use_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_unmarked_address_use_result);
  require(unsafe_stored_unmarked_address_use_reason.has_value() &&
              unsafe_stored_unmarked_address_use_reason->find("stored in R9 reaches К БП 9 "
                                                              "before K {x}") !=
                  std::string::npos &&
              unsafe_stored_unmarked_address_use_reason->find("(indirect address/control use)") !=
                  std::string::npos,
          "dead-integer rejection reason must identify unproved stored-selector address uses");

  CompileResult unsafe_dead_integer_result = safe_dead_integer_result;
  unsafe_dead_integer_result.steps.back() = resolved_step(0x12, "expr *");
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_dead_integer_result),
          "dead-integer fractional selector elision must reject any use before K {x} erases the "
          "retuned integer part");
  const std::optional<std::string> unsafe_dead_integer_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(safe_dead_integer_options,
                                                               unsafe_dead_integer_result);
  require(unsafe_dead_integer_reason.has_value() &&
              unsafe_dead_integer_reason->find("reaches expr * before K {x}") !=
                  std::string::npos &&
              unsafe_dead_integer_reason->find("(data arithmetic)") != std::string::npos,
          "dead-integer rejection reason must identify arithmetic consumers before fractional "
          "erase");

  CompileResult unsafe_stored_live_x_result = safe_stored_dead_integer_result;
  unsafe_stored_live_x_result.steps.at(unsafe_stored_live_x_result.steps.size() - 3U) =
      resolved_step(0x12, "expr *");
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_stored_live_x_result),
          "dead-integer fractional selector elision must reject direct store forwarding unless "
          "the still-live X value is erased immediately after the store");
  const std::optional<std::string> unsafe_stored_live_x_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_live_x_result);
  require(unsafe_stored_live_x_reason.has_value() &&
              unsafe_stored_live_x_reason->find("is stored before expr * instead of immediate "
                                                "K {x}") != std::string::npos,
          "dead-integer rejection reason must identify unsafe live-X use after store");
  require(unsafe_stored_live_x_reason->find("(data arithmetic)") != std::string::npos,
          "dead-integer rejection reason must classify unsafe live-X arithmetic after store");

  CompileResult unsafe_stored_recall_result = safe_stored_dead_integer_result;
  unsafe_stored_recall_result.steps.back() = resolved_step(0x12, "expr *");
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_stored_recall_result),
          "dead-integer fractional selector elision must reject stored selector recalls that are "
          "consumed before K {x}");
  const std::optional<std::string> unsafe_stored_recall_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_stored_recall_result);
  require(unsafe_stored_recall_reason.has_value() &&
              unsafe_stored_recall_reason->find("stored in R9 reaches expr * before K {x}") !=
                  std::string::npos,
          "dead-integer rejection reason must identify unsafe stored-register consumers");

  CompileResult unsafe_dead_integer_flow_result = safe_dead_integer_result;
  unsafe_dead_integer_flow_result.steps.back() =
      resolved_step_with_mnemonic(0x89, "К БП 9", "indirect flow before erase");
  require(!optimizer_static_proof_gate_accepts_for_testing(safe_dead_integer_options,
                                                           unsafe_dead_integer_flow_result),
          "dead-integer fractional selector elision must reject indirect flow before K {x} erases "
          "the retuned integer part");
  const std::optional<std::string> unsafe_dead_integer_flow_reason =
      optimizer_static_proof_gate_rejection_reason_for_testing(
          safe_dead_integer_options, unsafe_dead_integer_flow_result);
  require(unsafe_dead_integer_flow_reason.has_value() &&
              unsafe_dead_integer_flow_reason->find("reaches К БП 9 before K {x}") !=
                  std::string::npos &&
              unsafe_dead_integer_flow_reason->find("(indirect address/control use)") !=
                  std::string::npos,
          "dead-integer rejection reason must classify indirect flow before fractional erase");

  CompileResult forward_fractional_result = forward_result;
  forward_fractional_result.optimizations.push_back(
      optimization_report("fractional-constant-selector-use"));
  require(!optimizer_static_proof_gate_accepts_for_testing(forward_fractional_options,
                                                           forward_fractional_result),
          "forward fractional-selector candidate without data-value proof must be rejected");

  forward_fractional_result.steps.push_back(
      resolved_step(0x35, "fractional selector const 0.123456"));
  require(optimizer_static_proof_gate_accepts_for_testing(forward_fractional_options,
                                                          forward_fractional_result),
          "forward fractional-selector candidate with explicit data recovery should be accepted");

  CompileOptions forced_recovered_forward_fractional_options = forward_fractional_options;
  forced_recovered_forward_fractional_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(optimizer_static_proof_gate_accepts_for_testing(forced_recovered_forward_fractional_options,
                                                          forward_fractional_result),
          "forced fractional selector preload should be accepted when the same value is recovered "
          "by the К {x} proof marker");

  const std::optional<int> natural_target = core::resolve_flow_target("9", 0.123456);
  require(natural_target.has_value(), "natural fractional selector value should resolve statically");
  CompileOptions natural_forward_fractional_options = forward_options;
  natural_forward_fractional_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = *natural_target});
  CompileResult natural_forward_fractional_result;
  natural_forward_fractional_result.optimizations.push_back(
      optimization_report("preloaded-indirect-flow"));
  natural_forward_fractional_result.optimizations.push_back(
      optimization_report("natural-fractional-constant-selector-use"));
  natural_forward_fractional_result.preloads.push_back(flow_preload("9", "0.123456"));
  natural_forward_fractional_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "0.123456", *natural_target)));
  require(optimizer_static_proof_gate_accepts_for_testing(natural_forward_fractional_options,
                                                          natural_forward_fractional_result),
          "forward fractional-selector candidate with matching natural preload should be accepted");

  CompileOptions forced_natural_forward_fractional_options = natural_forward_fractional_options;
  forced_natural_forward_fractional_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(optimizer_static_proof_gate_accepts_for_testing(forced_natural_forward_fractional_options,
                                                          natural_forward_fractional_result),
          "forced fractional selector preload should be accepted when a matching stable natural "
          "preload proves the value");

  CompileOptions bad_natural_forward_fractional_options = natural_forward_fractional_options;
  CompileResult bad_natural_forward_fractional_result = natural_forward_fractional_result;
  const std::optional<int> bad_natural_target = core::resolve_flow_target("9", 0.654321);
  require(bad_natural_target.has_value(),
          "mismatched natural fractional selector value should resolve statically");
  bad_natural_forward_fractional_result.preloads.clear();
  bad_natural_forward_fractional_result.preloads.push_back(flow_preload("9", "0.654321"));
  bad_natural_forward_fractional_result.steps.clear();
  bad_natural_forward_fractional_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "0.654321", *bad_natural_target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(bad_natural_forward_fractional_options,
                                                           bad_natural_forward_fractional_result),
          "natural fractional-selector proof must reject preloaded values that do not match the "
          "required selector value");

  CompileOptions unstable_natural_forward_fractional_options = natural_forward_fractional_options;
  CompileResult unstable_natural_forward_fractional_result = natural_forward_fractional_result;
  unstable_natural_forward_fractional_result.preloads.clear();
  unstable_natural_forward_fractional_result.preloads.push_back(flow_preload("9", "0.654321"));
  unstable_natural_forward_fractional_result.preloads.push_back(flow_preload("3", "0.123456"));
  unstable_natural_forward_fractional_result.steps.clear();
  unstable_natural_forward_fractional_result.steps.push_back(
      resolved_step(0x89, indirect_target_comment("9", "0.654321", *bad_natural_target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(unstable_natural_forward_fractional_options,
                                                           unstable_natural_forward_fractional_result),
          "natural fractional-selector proof must reject matching values outside stable selector "
          "registers");

  CompileResult wrong_opcode_forward_fractional_result = forward_result;
  wrong_opcode_forward_fractional_result.optimizations.push_back(
      optimization_report("fractional-constant-selector-use"));
  wrong_opcode_forward_fractional_result.steps.push_back(
      resolved_step(0x36, "fractional selector const 0.123456"));
  require(!optimizer_static_proof_gate_accepts_for_testing(forward_fractional_options,
                                                           wrong_opcode_forward_fractional_result),
          "fractional-selector recovery proof marker must be attached to the К {x} opcode");

  CompileOptions partial_forward_fractional_options = forward_fractional_options;
  partial_forward_fractional_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.654321", .target = target});
  require(!optimizer_static_proof_gate_accepts_for_testing(partial_forward_fractional_options,
                                                           forward_fractional_result),
          "fractional-selector data proof must cover every selector value, not just one");

  CompileOptions forced_extra_forward_fractional_options = forward_fractional_options;
  forced_extra_forward_fractional_options.force_fractional_constant_selector_preloads.push_back(
      "0.654321");
  require(!optimizer_static_proof_gate_accepts_for_testing(forced_extra_forward_fractional_options,
                                                           forward_fractional_result),
          "fractional-selector data proof must cover every forced selector preload value");

  CompileOptions standalone_fractional_options;
  standalone_fractional_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = target});
  CompileResult standalone_fractional_result;
  standalone_fractional_result.optimizations.push_back(
      optimization_report("fractional-constant-selector-use"));
  standalone_fractional_result.steps.push_back(
      resolved_step(0x35, "fractional selector const 0.123456"));
  require(!optimizer_static_proof_gate_accepts_for_testing(standalone_fractional_options,
                                                           standalone_fractional_result),
          "fractional selector packing without an indirect-flow family gate must be rejected");

  CompileOptions standalone_forced_fractional_options;
  standalone_forced_fractional_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  CompileResult standalone_forced_fractional_result;
  require(!optimizer_static_proof_gate_accepts_for_testing(standalone_forced_fractional_options,
                                                           standalone_forced_fractional_result),
          "forced fractional selector preload without a proof route must be rejected");

  CompileOptions forced_fractional_with_preloaded_options = preloaded_options;
  forced_fractional_with_preloaded_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(!optimizer_static_proof_gate_accepts_for_testing(forced_fractional_with_preloaded_options,
                                                           preloaded_result),
          "preloaded indirect-flow proof must reject forced fractional selector preloads without a "
          "matching fractional-selector data proof");

  CompileOptions forced_fractional_with_forward_options = forward_options;
  forced_fractional_with_forward_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(!optimizer_static_proof_gate_accepts_for_testing(forced_fractional_with_forward_options,
                                                           forward_result),
          "forward indirect-flow proof must reject forced fractional selector preloads without a "
          "matching fractional-selector data proof");

  CompileOptions forced_fractional_with_forward_plan_options = forward_options;
  forced_fractional_with_forward_plan_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = target});
  forced_fractional_with_forward_plan_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              forced_fractional_with_forward_plan_options, forward_result),
          "forward indirect-flow proof must reject forced fractional selector preloads even when a "
          "fractional selector plan exists but no data proof is present");

  CompileOptions forced_fractional_with_dual_use_plan_options;
  forced_fractional_with_dual_use_plan_options.dual_use_constant_indirect_flow = true;
  forced_fractional_with_dual_use_plan_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = target});
  forced_fractional_with_dual_use_plan_options.force_fractional_constant_selector_preloads.push_back(
      "0.123456");
  require(!optimizer_static_proof_gate_accepts_for_testing(
              forced_fractional_with_dual_use_plan_options, preloaded_result),
          "dual-use indirect-flow proof must reject forced fractional selector preloads even when "
          "a fractional selector plan exists but no data proof is present");

  CompileOptions runtime_options;
  runtime_options.runtime_indirect_call_flow = true;
  CompileResult runtime_result;
  runtime_result.optimizations.push_back(optimization_report("runtime-indirect-call-flow"));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options, runtime_result),
          "runtime indirect-call candidate without target proof must be rejected");

  CompileResult fabricated_runtime_report = runtime_result;
  fabricated_runtime_report.proofs.push_back(proved_report("runtime-indirect-call-targets"));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           fabricated_runtime_report),
          "runtime indirect-call candidate must not be accepted by a fabricated proof report");

  CompileResult runtime_bad_order_result = runtime_result;
  runtime_bad_order_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  runtime_bad_order_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_bad_order_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           runtime_bad_order_result),
          "runtime indirect-call candidate must reject selector stores that appear after the call");

  CompileResult runtime_overwrite_result = runtime_result;
  runtime_overwrite_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_overwrite_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_overwrite_result.steps.push_back(resolved_step(0x49, "unrelated store"));
  runtime_overwrite_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           runtime_overwrite_result),
          "runtime indirect-call candidate must reject selector registers overwritten before call");

  CompileResult runtime_unstable_call_result = runtime_result;
  runtime_unstable_call_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_unstable_call_result.steps.push_back(
      resolved_step(0x43, "runtime indirect call selector " + std::to_string(target)));
  runtime_unstable_call_result.steps.push_back(
      resolved_step(0xa3, runtime_indirect_target_comment("3", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           runtime_unstable_call_result),
          "runtime indirect-call candidate must reject annotated calls through unstable registers");

  CompileResult runtime_missing_literal_result = runtime_result;
  runtime_missing_literal_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_missing_literal_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           runtime_missing_literal_result),
          "runtime indirect-call candidate must reject selector stores without target literal digits");

  CompileResult runtime_embedded_marker_result = runtime_result;
  runtime_embedded_marker_result.steps.push_back(
      resolved_step(3, "note: runtime indirect call selector " + std::to_string(target)));
  runtime_embedded_marker_result.steps.push_back(
      resolved_step(0x49, "note: runtime indirect call selector " + std::to_string(target)));
  runtime_embedded_marker_result.steps.push_back(
      resolved_step(0xa9, "note: " +
                              runtime_indirect_target_comment("9", std::to_string(target),
                                                               target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(runtime_options,
                                                           runtime_embedded_marker_result),
          "runtime indirect-call proof markers must be prefix-based, not embedded text");

  CompileResult runtime_bad_selector_marker_result = runtime_result;
  runtime_bad_selector_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target) + "garbage"));
  runtime_bad_selector_marker_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target) + "garbage"));
  runtime_bad_selector_marker_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              runtime_options, runtime_bad_selector_marker_result),
          "runtime indirect-call proof must reject malformed selector markers");

  CompileResult runtime_malformed_store_marker_result = runtime_result;
  runtime_malformed_store_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_store_marker_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target) +
                              "garbage"));
  runtime_malformed_store_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_store_marker_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_store_marker_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              runtime_options, runtime_malformed_store_marker_result),
          "runtime indirect-call proof must reject malformed selector store markers even when a "
          "later store/call pair is proved");

  CompileResult runtime_malformed_digit_marker_result = runtime_result;
  runtime_malformed_digit_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target) + "garbage"));
  runtime_malformed_digit_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_digit_marker_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_digit_marker_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              runtime_options, runtime_malformed_digit_marker_result),
          "runtime indirect-call proof must reject malformed selector digit markers even when a "
          "later store/call pair is proved");

  CompileResult runtime_out_of_range_target_result = runtime_result;
  runtime_out_of_range_target_result.steps.push_back(
      resolved_step(1, "runtime indirect call selector 105"));
  runtime_out_of_range_target_result.steps.push_back(
      resolved_step(0, "runtime indirect call selector 105"));
  runtime_out_of_range_target_result.steps.push_back(
      resolved_step(5, "runtime indirect call selector 105"));
  runtime_out_of_range_target_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector 105"));
  runtime_out_of_range_target_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", "105", 105)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              runtime_options, runtime_out_of_range_target_result),
          "runtime indirect-call proof must reject out-of-range target markers");

  CompileResult runtime_malformed_call_marker_result = runtime_result;
  runtime_malformed_call_marker_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_call_marker_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_malformed_call_marker_result.steps.push_back(
      resolved_step(0xa9, "runtime indirect call; preloaded R9=" + std::to_string(target) +
                              " indirect-target=" + std::to_string(target) + "garbage"));
  runtime_malformed_call_marker_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(!optimizer_static_proof_gate_accepts_for_testing(
              runtime_options, runtime_malformed_call_marker_result),
          "runtime indirect-call proof must reject malformed call target markers even when another "
          "runtime call is proved");

  runtime_result.steps.push_back(
      resolved_step(3, "runtime indirect call selector " + std::to_string(target)));
  runtime_result.steps.push_back(
      resolved_step(0x49, "runtime indirect call selector " + std::to_string(target)));
  runtime_result.steps.push_back(
      resolved_step(0xa9, runtime_indirect_target_comment("9", std::to_string(target), target)));
  require(optimizer_static_proof_gate_accepts_for_testing(runtime_options, runtime_result),
          "runtime indirect-call candidate with verified runtime selector artifact should be accepted");

  CompileOptions aggressive_runtime_options;
  aggressive_runtime_options.aggressive_indirect_call = true;
  require(!optimizer_static_proof_gate_accepts_for_testing(aggressive_runtime_options,
                                                           fabricated_runtime_report),
          "aggressive indirect-call candidate must not bypass runtime proof obligations");
  require(optimizer_static_proof_gate_accepts_for_testing(aggressive_runtime_options,
                                                          runtime_result),
          "aggressive indirect-call candidate with verified runtime selector artifact should be accepted");

  CompileOptions threshold_runtime_options;
  threshold_runtime_options.aggressive_indirect_call_threshold = true;
  require(!optimizer_static_proof_gate_accepts_for_testing(threshold_runtime_options,
                                                           fabricated_runtime_report),
          "aggressive indirect-call threshold candidate must not bypass runtime proof obligations");
  require(optimizer_static_proof_gate_accepts_for_testing(threshold_runtime_options,
                                                          runtime_result),
          "aggressive indirect-call threshold candidate with verified runtime selector artifact should be accepted");
}

void optimizer_translation_unit_stays_emulator_free() {
  const std::string compiler_source =
      read_text_file(project_file("native/src/core/compiler.cpp"));
  const std::string compiler_without_comments = source_without_comments(compiler_source);
  const std::string compiler_code = code_without_comments_and_literals(compiler_source);
  require(!contains_emulator_include(compiler_without_comments),
          "compiler.cpp must not include emulator headers; behavior digest belongs to the debug "
          "translation unit");
  require(!contains_function_call_token(compiler_code, "program_behavior_digest"),
          "compiler.cpp must not define or call the emulator-backed behavior digest");
  require(!contains_function_call_token(compiler_code, "run_equivalence_observation"),
          "compiler.cpp must not define or call the emulator equivalence observation harness");
  require(!contains_function_call_token(compiler_code, "equivalence_observations"),
          "compiler.cpp must not define or call the emulator equivalence observation harness");
  const std::string candidate_gate_predicate =
      required_source_range(compiler_code, "bool candidate_needs_static_proof_gate",
                            "bool suppressed_constant_preloads_proved");
  require(candidate_gate_predicate.find("aggressive_post_layout_indirect_flow") !=
              std::string::npos,
          "static proof-gate predicate must cover aggressive indirect-flow candidates");
  require(candidate_gate_predicate.find("dual_use_constant_indirect_flow") != std::string::npos,
          "static proof-gate predicate must cover dual-use indirect-flow candidates");
  require(candidate_gate_predicate.find("runtime_indirect_call_flow") != std::string::npos,
          "static proof-gate predicate must cover runtime indirect-call candidates");
  require(candidate_gate_predicate.find("aggressive_indirect_call") != std::string::npos,
          "static proof-gate predicate must cover aggressive indirect-call candidates");
  require(candidate_gate_predicate.find("aggressive_indirect_call_threshold") !=
              std::string::npos,
          "static proof-gate predicate must cover aggressive indirect-call threshold candidates");
  require(candidate_gate_predicate.find("preloaded_indirect_flow") != std::string::npos,
          "static proof-gate predicate must cover preloaded indirect-flow candidates");
  require(candidate_gate_predicate.find("forward_indirect_flow") != std::string::npos,
          "static proof-gate predicate must cover forward indirect-flow candidates");
  require(candidate_gate_predicate.find("assume_dead_selector_integer_part") != std::string::npos,
          "static proof-gate predicate must reject dead-integer selector assumptions");
  require(candidate_gate_predicate.find("suppress_constant_preloads") != std::string::npos,
          "static proof-gate predicate must cover suppress-only preload candidates");
  require(candidate_gate_predicate.find("fractional_constant_selectors") != std::string::npos,
          "static proof-gate predicate must cover fractional selector candidates");
  require(candidate_gate_predicate.find("force_fractional_constant_selector_preloads") !=
              std::string::npos,
          "static proof-gate predicate must cover forced fractional selector preloads");
  require(candidate_gate_predicate.find("synthesized_dispatch_plans") != std::string::npos,
          "static proof-gate predicate must cover computed-dispatch candidates");
  const std::string static_gate_source =
      required_source_range(compiler_code, "bool computed_dispatch_static_gate_accepts",
                            "bool is_only_budget_exceeded");
  require(static_gate_source.find(".proofs") == std::string::npos,
          "optimizer static proof gates must not trust CompileResult::proofs");
  require(static_gate_source.find("program_behavior_digest") == std::string::npos,
          "optimizer static proof gates must not call the behavior digest");
  require(static_gate_source.find("run_equivalence") == std::string::npos,
          "optimizer static proof gates must not call the emulator equivalence harness");
  require(static_gate_source.find("emulator::") == std::string::npos,
          "optimizer static proof gates must not instantiate the emulator");
  require(static_gate_source.find("indirect_flow_targets_proved(result.optimizations, "
                                  "result.preloads") != std::string::npos,
          "optimizer static gates must pass final preload artifacts to indirect-flow verifier");
  require(static_gate_source.find("result.registers") != std::string::npos,
          "optimizer static gates must pass allocated registers to indirect-flow verifier");
  require(static_gate_source.find("indirect_flow_targets_proved(result.optimizations, "
                                  "candidate_options") == std::string::npos,
          "optimizer static gates must not pass compile options as indirect-flow proof input");
  require(static_gate_source.find("computed_dispatch_artifacts_proved(candidate_options, "
                                  "result.steps") != std::string::npos,
          "computed-dispatch gate must require final emitted dispatch artifacts");
  const std::string computed_dispatch_artifact_source =
      required_source_range(compiler_code, "bool computed_dispatch_artifacts_proved",
                            "bool computed_dispatch_static_gate_accepts");
  require(computed_dispatch_artifact_source.find("computed_dispatch_proof_targets_from_comment") !=
              std::string::npos,
          "computed-dispatch verifier must parse final numeric proof-target artifacts");
  require(computed_dispatch_artifact_source.find("is_computed_dispatch_comment") !=
              std::string::npos,
          "computed-dispatch verifier must require the computed-dispatch proof-comment prefix");
  const std::string preload_verifier_source =
      required_source_range(compiler_code, "bool final_preload_resolves_to_flow_target",
                            "bool suppressed_constant_preloads_proved");
  require(preload_verifier_source.find("bool preload_resolves_to_flow_target") ==
              std::string::npos,
          "indirect-flow preload verifier must keep the final-artifact-only helper name");
  require(preload_verifier_source.find("const std::vector<PreloadReport>& preloads") !=
              std::string::npos,
          "indirect-flow preload verifier must inspect final preload artifacts");
  require(preload_verifier_source.find("preloaded_constant_registers") == std::string::npos,
          "indirect-flow preload verifier must not trust compile options as proof");
  const std::string indirect_flow_verifier_source = required_source_range_after(
      compiler_code, "bool final_preload_resolves_to_flow_target",
      "bool indirect_flow_targets_proved", "bool fractional_selector_data_values_proved");
  require(indirect_flow_verifier_source.find("const std::vector<PreloadReport>& preloads") !=
              std::string::npos,
          "indirect-flow target verifier must accept final preload artifacts");
  require(indirect_flow_verifier_source.find("CompileOptions") == std::string::npos,
          "indirect-flow target verifier must not accept compile options as proof input");
  const std::string proof_report_source =
      required_source_range(compiler_without_comments,
                            "std::vector<ProofReport> build_proof_report",
                            "void populate_public_report");
  require(proof_report_source.find("indirect_flow_targets_proved") != std::string::npos,
          "ProofReport must mirror the indirect-flow verifier result");
  require(proof_report_source.find("Final preload artifacts for indirect branch/call selectors") !=
              std::string::npos,
          "ProofReport detail must describe indirect-flow proof as final-preload-artifact based");
  require(proof_report_source.find("synthesized_dispatch_plans_proved") != std::string::npos,
          "ProofReport must mirror the computed-dispatch verifier result");
  require(proof_report_source.find("computed_dispatch_artifacts_proved") != std::string::npos,
          "ProofReport must mirror the computed-dispatch final-artifact verifier result");
  require(proof_report_source.find("matched final indirect-jump target/proof-target markers") !=
              std::string::npos,
          "ProofReport detail must describe computed-dispatch proof as final-artifact based");
  require(proof_report_source.find("runtime_indirect_call_targets_proved") != std::string::npos,
          "ProofReport must mirror the runtime indirect-call verifier result");
  require(proof_report_source.find("fractional_selector_data_values_proved") != std::string::npos,
          "ProofReport must mirror the fractional-selector data verifier result");
  require(proof_report_source.find("Every required fractional selector value was proved from final "
                                   "artifacts") != std::string::npos,
          "ProofReport detail must describe fractional-selector proof as final-artifact based");
  require(proof_report_source.find("suppressed_constant_preloads_proved") != std::string::npos,
          "ProofReport must mirror the suppressed-preload verifier result");
  require(proof_report_source.find("preloaded_constant_registers_proved") != std::string::npos,
          "ProofReport must mirror the explicit-preload verifier result");
  require(proof_report_source.find("address_code_overlay_artifacts_proved") != std::string::npos,
          "ProofReport must mirror the address/code overlay final-artifact verifier result");
  require(proof_report_source.find("has_optimization_named(optimizations, \"address-code-overlay\")") ==
              std::string::npos,
          "ProofReport must not trust the address-code-overlay optimization label as proof");
  require(proof_report_source.find("Final address/code overlay artifacts matched") !=
              std::string::npos,
          "ProofReport detail must describe address/code overlay proof as final-artifact based");
  const std::string fractional_verifier_source = required_source_range_after(
      compiler_code, "bool has_fractional_selector_recovery_step",
      "bool fractional_selector_data_values_proved",
      "std::vector<ProofReport> build_proof_report");
  const std::string fractional_recovery_helper_source =
      required_source_range(compiler_code, "bool has_fractional_selector_recovery_step",
                            "std::optional<std::string> indirect_flow_register_for_opcode");
  const std::string fractional_verifier_function_source =
      required_source_range_after(compiler_code, "bool has_fractional_selector_recovery_step",
                                  "bool fractional_selector_data_values_proved",
                                  "std::vector<ProofReport> build_proof_report");
  require(fractional_verifier_source.find("required_values") != std::string::npos,
          "fractional-selector verifier must collect the complete set of required values");
  require(fractional_verifier_source.find("fractional_constant_selectors") != std::string::npos,
          "fractional-selector verifier must include selector plan values");
  require(fractional_verifier_source.find("force_fractional_constant_selector_preloads") !=
              std::string::npos,
          "fractional-selector verifier must include forced selector preload values");
  require(fractional_verifier_source.find("is_stable_indirect_selector") != std::string::npos,
          "fractional-selector natural preload proof must require a stable selector register");
  require(fractional_verifier_source.find("const std::vector<PreloadReport>& preloads") !=
              std::string::npos,
          "fractional-selector natural preload proof must inspect final preload artifacts");
  require(fractional_verifier_function_source.find("preloaded_constant_registers") ==
              std::string::npos,
          "fractional-selector natural preload proof must not trust compile options as proof");
  require(fractional_verifier_source.find("for (const std::string& expected : required_values)") !=
              std::string::npos,
          "fractional-selector verifier must prove every required value individually");
  require(fractional_verifier_source.find("has_fractional_selector_recovery_step") !=
              std::string::npos,
          "fractional-selector verifier must require recovery proof markers on recovery opcodes");
  require(fractional_recovery_helper_source.find("kFractionalSelectorRecoveryOpcode") !=
              std::string::npos,
          "fractional-selector recovery proof must use the named К {x} opcode constant");
  require(fractional_verifier_source.find("has_resolved_step_comment") == std::string::npos,
          "fractional-selector verifier must not accept recovery proof markers on arbitrary steps");
  require(fractional_verifier_source.find("saw_recovery") == std::string::npos,
          "fractional-selector verifier must not accept a single recovered value as proof for all "
          "required values");
  require(fractional_verifier_source.find("saw_natural_value") == std::string::npos,
          "fractional-selector verifier must not accept a single natural value as proof for all "
          "required values");
  require(compiler_code.find("candidate_needs_static_proof_gate(candidate.options)") !=
              std::string::npos,
          "main optimizer candidate loop must ask whether a candidate needs static proof");
  require(compiler_code.find("optimizer_static_gate_rejection_reason(candidate.options, result)") !=
              std::string::npos,
          "main optimizer candidate loop must reject and report unproved risky candidates");
  require(compiler_code.find("candidate_needs_static_proof_gate(candidate_options)") !=
              std::string::npos,
          "optimizer beam search must ask whether a candidate needs static proof");
  require(compiler_code.find(
              "optimizer_static_gate_rejection_reason(candidate_options, result)") !=
              std::string::npos,
          "optimizer beam search must reject unproved risky candidates through the same static "
          "proof reason helper");

  const std::string wrapper_source = read_text_file(project_file("native/include/mkpro/compiler.hpp"));
  const std::string wrapper_without_comments = source_without_comments(wrapper_source);
  require(wrapper_without_comments.find("compiler_behavior_digest.hpp") == std::string::npos,
          "public compiler wrapper must not re-export the emulator-backed behavior digest API");

  const std::string compiler_header =
      source_without_comments(read_text_file(project_file("native/include/mkpro/core/compiler.hpp")));
  require(compiler_header.find("program_behavior_digest") == std::string::npos,
          "core compiler API must not declare the emulator-backed behavior digest");
  require(compiler_header.find("optimizer_static_proof_gate_accepts_for_testing") ==
              std::string::npos,
          "core compiler API must not declare the static proof gate test hook");

  const std::string digest_header = source_without_comments(
      read_text_file(project_file("native/include/mkpro/core/compiler_behavior_digest.hpp")));
  require(digest_header.find("program_behavior_digest") != std::string::npos,
          "behavior digest API should remain available only through its explicit debug header");
  require(digest_header.find("compile_source") == std::string::npos,
          "behavior digest header must not become a replacement production compiler API");

  const std::string proof_gate_header = source_without_comments(
      read_text_file(project_file("native/include/mkpro/core/compiler_static_proof_gate.hpp")));
  require(proof_gate_header.find("optimizer_static_proof_gate_accepts_for_testing") !=
              std::string::npos,
          "static proof gate test hook should remain available only through its explicit header");
  require(proof_gate_header.find("optimizer_static_proof_gate_rejection_reason_for_testing") !=
              std::string::npos,
          "static proof gate rejection-reason test hook should remain available only through its "
          "explicit header");
  require(proof_gate_header.find("compile_source") == std::string::npos,
          "static proof gate header must not become a replacement production compiler API");
  require(proof_gate_header.find("program_behavior_digest") == std::string::npos,
          "static proof gate header must not expose the emulator-backed behavior digest");
  require(proof_gate_header.find("mkpro/emulator") == std::string::npos,
          "static proof gate header must not depend on emulator headers");

  const std::string digest_impl =
      source_without_comments(read_text_file(project_file("native/src/core/compiler_behavior_digest.cpp")));
  require(contains_emulator_include(digest_impl),
          "behavior digest implementation should be the explicit emulator-owning translation unit");
  require(digest_impl.find("optimizer_static_proof_gate_accepts_for_testing") == std::string::npos,
          "behavior digest implementation must not expose the static proof gate test hook");
  require(digest_impl.find("optimizer_static_gate_accepts") == std::string::npos,
          "behavior digest implementation must not participate in optimizer candidate acceptance");

  const std::string cmake_source = read_text_file(project_file("native/CMakeLists.txt"));
  require(contains_noncomment_line_text(cmake_source, "src/core/compiler.cpp"),
          "CMake must keep the optimizer translation unit explicit");
  const std::string core_target_source =
      required_source_range(cmake_source, "add_library(mkpro_core STATIC",
                            "add_library(mkpro_debug STATIC");
  require(core_target_source.find("src/core/compiler_behavior_digest.cpp") == std::string::npos,
          "mkpro_core must not include the emulator-backed behavior digest translation unit");
  require(core_target_source.find("src/emulator/") == std::string::npos,
          "mkpro_core must not include emulator translation units");
  const std::string debug_target_source =
      required_source_range(cmake_source, "add_library(mkpro_debug STATIC",
                            "add_executable(mkpro-native");
  require(contains_noncomment_line_text(cmake_source, "src/core/compiler_behavior_digest.cpp"),
          "CMake must keep the emulator-backed behavior digest in its separate translation unit");
  require(debug_target_source.find("src/core/compiler_behavior_digest.cpp") != std::string::npos,
          "mkpro_debug must own the emulator-backed behavior digest translation unit");
  require(debug_target_source.find("src/emulator/mk61.cpp") != std::string::npos,
          "mkpro_debug must own the MK-61 emulator implementation");
  require(debug_target_source.find("src/emulator/rom.cpp") != std::string::npos,
          "mkpro_debug must own the MK-61 emulator ROM implementation");
  require(cmake_source.find("target_link_libraries(mkpro-native PRIVATE mkpro_debug)") !=
              std::string::npos,
          "CLI may use behavior digest only through mkpro_debug, not mkpro_core");
  require(cmake_source.find("target_link_libraries(mkpro_tests PRIVATE mkpro_debug)") !=
              std::string::npos,
          "tests may use emulator-backed validation only through mkpro_debug, not mkpro_core");
  require(count_noncomment_lines_with_text(cmake_source, "src/core/compiler.cpp") == 1,
          "CMake must list compiler.cpp exactly once as the optimizer translation unit");
  require(count_noncomment_lines_with_text(cmake_source, "src/core/compiler_behavior_digest.cpp") ==
              1,
          "CMake must list compiler_behavior_digest.cpp exactly once as the debug digest "
          "translation unit");
}

}  // namespace mkpro::tests
