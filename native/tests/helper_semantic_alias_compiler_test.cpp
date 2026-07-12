#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& report) { return report.name == name; });
}

std::optional<std::string> optimization_detail(const CompileResult& result,
                                               const std::string& name) {
  const auto found =
      std::find_if(result.optimizations.begin(), result.optimizations.end(),
                   [&](const OptimizationReport& report) { return report.name == name; });
  return found == result.optimizations.end() ? std::nullopt
                                             : std::optional<std::string>{found->detail};
}

CompileResult compile_domain_fixture(const std::string& mutation) {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.x_param_value_functions = true;
  return compile_source(R"mkpro(
program OpaqueDomainFixture {
  state {
    axis: packed = 0
    bounded_axis: counter 1..4 = 1
    zero_axis: counter 0..0 = 0
    selector: packed = 0
    line: packed = 0
    first: packed = 0
    second: packed = 0
    third: packed = 0
    fourth: packed = 0
    fifth: packed = 0
    sixth: packed = 0
    seventh: packed = 0
    eighth: packed = 0
  }

  loop {
)mkpro" + mutation + R"mkpro(
    normalize(axis)
    first = line
    normalize(axis)
    second = line
    normalize(axis)
    third = line
    normalize(axis)
    fourth = line
    fifth = grid_norm(axis)
    sixth = grid_norm(axis)
    seventh = grid_norm(axis)
    eighth = grid_norm(axis)
    halt(first + second + third + fourth + fifth + sixth + seventh + eighth)
  }

  fn normalize(value) {
    line = frac((value + 7) / 4) * 4 + 1
  }

  fn sanitize() {
    axis = grid_norm(axis)
  }
}
)mkpro",
                        options);
}

std::string alias_composition_fixture_source(const std::string& mutation) {
  return R"mkpro(
program OpaqueAliasCompositionFixture {
  state {
    seed0: packed = 11
    seed1: packed = 12
    seed2: packed = 13
    seed3: packed = 14
    seed4: packed = 15
    seed5: packed = 16
    seed6: packed = 17
    sample: counter 0..4 = 0
    out: packed = 0
    total: packed = 0
    noise0: packed = 0
    noise1: packed = 0
    noise2: packed = 0
    noise3: packed = 0
    noise4: packed = 0
  }

  loop {
    noise0 += 80
    noise1 += 80
    noise2 += 80
    noise3 += 80
    noise4 += 80
)mkpro" + mutation +
         R"mkpro(
    transform(sample)
    total += out
    transform(sample)
    total += out
    transform(sample)
    total += out
    transform(sample)
    total += out
    total += grid_norm(sample)
    total += grid_norm(sample)
    total += grid_norm(sample)
    total += grid_norm(sample)
    halt(total + seed0 + seed1 + seed2 + seed3 + seed4 + seed5 + seed6 + noise0 + noise1 + noise2 + noise3 + noise4)
  }

  fn transform(value) {
    out = frac((value + 7) / 4) * 4 + 1
  }
}
)mkpro";
}

CompileResult compile_alias_composition_fixture(const std::string& mutation) {
  CompileOptions options;
  options.analysis = true;
  options.budget = 105;
  options.disable_candidate_search = true;
  options.x_param_value_functions = true;
  options.general_constant_preloads = true;
  return compile_source(alias_composition_fixture_source(mutation), options);
}

CompileResult compile_alias_composition_with_candidates() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.fast_candidate_search = true;
  return compile_source(alias_composition_fixture_source(""), options);
}

} // namespace

void helper_semantic_alias_compiler_composes_with_natural_target_layout() {
  const CompileResult safe = compile_alias_composition_fixture("");
  require(safe.implemented, "opaque helper-alias composition fixture should compile");
  require(safe.steps.size() <= 105,
          "the proved helper alias and downstream layout should fit the MK-61 window");
  require(has_optimization(safe, "helper-semantic-alias"),
          "the compiler should apply the generic typed helper alias");
  require(has_optimization(safe, "natural-target-component-layout"),
          "the helper alias should compose atomically with the generic final layout proof");
  const std::string alias_detail = optimization_detail(safe, "helper-semantic-alias").value_or("");
  require(alias_detail.find("all 4 opaque source-call origin(s)") != std::string::npos,
          "the applied alias should account for every opaque source call origin");
  require(alias_detail.find("redundant 10-cell unary-X helper") != std::string::npos,
          "the applied alias should report the independently measured removed helper body");

  const CompileResult selected = compile_alias_composition_with_candidates();
  require(selected.implemented,
          "candidate search should finalize the opaque helper-layout fixture successfully");
  require(selected.steps.size() == safe.steps.size(),
          "late candidate finalization should recover the explicit proved helper layout");
  require(has_optimization(selected, "late-final-layout-candidate-search"),
          "candidate search should compare proof-gated final layouts after ordinary discovery");
  require(has_optimization(selected, "helper-semantic-alias") &&
              has_optimization(selected, "natural-target-component-layout"),
          "the finalized unrelated fixture should retain both independent final-layout proofs");

  const CompileResult unsafe = compile_alias_composition_fixture("    sample = random()\n");
  require(unsafe.implemented, "unknown-input helper-alias fixture should still compile");
  require(!has_optimization(unsafe, "helper-semantic-alias-domain-proof"),
          "an unknown source-call input must not issue a finite typed domain certificate");
  require(!has_optimization(unsafe, "helper-semantic-alias"),
          "the final optimizer must reject an alias without the typed domain certificate");
}

void helper_semantic_alias_compiler_rejects_unproved_source_domains() {
  const std::string proof = "helper-semantic-alias-domain-proof";
  const CompileResult safe = compile_domain_fixture("");
  require(safe.implemented, "opaque safe-domain fixture should compile");
  require(has_optimization(safe, proof),
          "an initialized exact value should issue typed helper contracts");
  require(
      optimization_detail(safe, proof)
              .value_or("")
              .find("Certified 4 direct helper call-site(s) with finite integral union [0, 0]") !=
          std::string::npos,
      "the report should expose the certified call count and exact union domain");

  const CompileResult sanitized = compile_domain_fixture("    bounded_axis = entered(1, 4)\n"
                                                         "    axis = bounded_axis\n"
                                                         "    sanitize()\n"
                                                         "    selector = 2\n"
                                                         "    while selector >= 1 {\n"
                                                         "      sanitize()\n"
                                                         "      selector--\n"
                                                         "    }\n");
  require(sanitized.implemented, "flow-sanitized packed fixture should compile");
  require(
      optimization_detail(sanitized, proof)
              .value_or("")
              .find("Certified 4 direct helper call-site(s) with finite integral union [1, 4]") !=
          std::string::npos,
      "interprocedural sanitization and a converged loop should prove the call-site domain");

  const CompileResult unbounded_sanitizer = compile_domain_fixture("    axis = entered()\n"
                                                                   "    sanitize()\n");
  require(unbounded_sanitizer.implemented,
          "unbounded grid-normalization negative fixture should compile");
  require(!has_optimization(unbounded_sanitizer, proof),
          "rounded grid_norm must not invent a finite integral domain for an unbounded operand");

  const CompileResult canonical_subtraction =
      compile_domain_fixture("    bounded_axis = entered(1, 4)\n"
                             "    axis = bounded_axis\n"
                             "    sanitize()\n"
                             "    selector = axis\n"
                             "    axis -= selector\n");
  require(canonical_subtraction.implemented,
          "positive-equality subtraction fixture should compile");
  require(optimization_detail(canonical_subtraction, proof)
                  .value_or("")
                  .find("finite integral union [-3, 3]") != std::string::npos,
          "subtracting values that can be equal only while strictly positive should certify +0");

  const CompileResult unbounded = compile_domain_fixture("    axis = entered()\n");
  require(unbounded.implemented, "plain entered() negative fixture should still compile");
  require(!has_optimization(unbounded, proof),
          "plain entered() must invalidate a declared counter bound before helper calls");

  const CompileResult refined_entered_zero = compile_domain_fixture("    axis = entered()\n"
                                                                    "    if axis != 0 {\n"
                                                                    "      halt(0)\n"
                                                                    "    }\n");
  require(refined_entered_zero.implemented,
          "entered zero-refinement negative fixture should still compile");
  require(!has_optimization(refined_entered_zero, proof),
          "numeric equality with zero must not turn an entered -0 into a certified +0");

  const CompileResult signed_zero_branch_laundering =
      compile_domain_fixture("    zero_axis = entered(0, 0)\n"
                             "    axis = zero_axis\n"
                             "    if axis >= 0 {\n"
                             "      axis = 1\n"
                             "    }\n"
                             "    else {\n"
                             "      axis = random()\n"
                             "    }\n");
  require(signed_zero_branch_laundering.implemented,
          "signed-zero branch-laundering negative fixture should compile");
  require(!has_optimization(signed_zero_branch_laundering, proof),
          "a numeric sign refinement must not erase the hardware -0 branch before an exact "
          "assignment");

  const CompileResult rounded_cancellation = compile_domain_fixture("    axis = 99999999\n"
                                                                    "    axis += 2\n"
                                                                    "    axis -= 100000000\n");
  require(rounded_cancellation.implemented,
          "rounded-cancellation negative fixture should still compile");
  require(!has_optimization(rounded_cancellation, proof),
          "an exact-in-Int128 but rounded-on-MK-61 argument derivation must not be certified");

  const CompileResult rounded_comparison_laundering =
      compile_domain_fixture("    axis = 99999999\n"
                             "    axis += 2\n"
                             "    axis -= 100000000\n"
                             "    if axis == 1 {\n"
                             "      axis = 1\n"
                             "    }\n"
                             "    else {\n"
                             "      axis = random()\n"
                             "    }\n");
  require(rounded_comparison_laundering.implemented,
          "rounded-comparison laundering negative fixture should compile");
  require(!has_optimization(rounded_comparison_laundering, proof),
          "an inexact abstract singleton must not eliminate the calculator's rounded branch");

  const CompileResult finite_out_of_declaration = compile_domain_fixture("    axis = 99\n");
  require(finite_out_of_declaration.implemented,
          "finite out-of-declaration fixture should still compile");
  require(optimization_detail(finite_out_of_declaration, proof)
                  .value_or("")
                  .find("finite integral union [99, 99]") != std::string::npos,
          "the proof should follow actual finite flow, not a declared counter promise");

  const CompileResult unknown_branch = compile_domain_fixture("    selector = random()\n"
                                                              "    if selector != 0 {\n"
                                                              "      axis = entered()\n"
                                                              "    }\n");
  require(unknown_branch.implemented, "unknown-branch negative fixture should still compile");
  require(!has_optimization(unknown_branch, proof),
          "an unsanitized unknown branch must invalidate the helper call-site domain");

  const CompileResult unknown_match = compile_domain_fixture("    selector = random()\n"
                                                             "    match selector {\n"
                                                             "      1 => axis = random()\n"
                                                             "      otherwise => axis = 0\n"
                                                             "    }\n");
  require(unknown_match.implemented, "unknown-match negative fixture should still compile");
  require(!has_optimization(unknown_match, proof),
          "match alternatives must be joined and retain an unknown arm");
}

} // namespace mkpro::tests
